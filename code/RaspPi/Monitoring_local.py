#
# This program provides two functons:
# 1) Read sensors that are locally connected to the Pi and
#    send the data via mqtt to mosquitto
#    - motion detector via gpio (interrupt driven)
#      .external pull-up and capacitor
#      .output stays high for about 6 seconds when motion is detected
# 2) Perform some local alarming logic
#
# ACKNOWLEDGEMENT:
#    I have benefitted greatly from many more experienced python developers
#    than I can keep track of.  So, if you see code snippets that you recognise: thanks !
#    Sorry that I couldn't remember and/or list all of the internet community individually.
#
# Having said that ...
#
#    This file is part of the "Monitoring zimKnives" project, which is a collection
#    of files, including this one.
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of these words as published by the author.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  Use at your own risk.
#
# v0.5:
# + added conf["LOCATION"] to be more specific with messages
# + added sending email messages on motion event ... it works !
# + added the holdoff timer for the motion event ... cool !
# + added periodic notification of t/h/g values
# + corrected bug where subscribed parameters were all being set as raw strings
# + moved all of the hardware i/o to the MonitoringParameters Env() class
# + made json decoding option in the on_message method
# + fixed a bug using bool() with strings in on_message
# + finish up the override logic for light and auto
# 
#
# v0.4:
# + separated the Env and Parm classes into a separate file
#   (added arguments to Env)
# + added structure for alarm stimulus/response processing
#   (demonsrated with motion->light = on ... cool!)
# + added the first simple alarm stimulus/response: motion/light
#
# v0.3:
# + enhanced the logging a bit, including logging to a file
# + added the parm and env class to organize the data
# + modified on_message and others to use the zkshop instance of env
# + tested with the o_light
# + created a separate configuration file ... more to come
#
# v0.2:
# + added mqtt.loop'ing stuff
# + rearranged for readability
# + implement callback on button push and handling for turning the light on
#
# v0.1:
# + very basic functionality and some experimentation
#
# Pending:
# + need to clean up properly from auto mode ... especially timers
# + add thresholds for t/h/g to change from notifications to alarm
# + command line arguments for logging level, filename, etc
# + implement mqtt.reconnect() logic for lost connection
# + configure the location and use to create the mqtt topics
#   ... make the data class instance name a bit more generic
# + maybe collapse process_overrides() and process_stimuluses() into just stimuluses
#

import paho.mqtt.client as mqtt
import time
import subprocess
import logging
import sys
import threading
from Monitoring_conf import conf
from MonitoringParameters import *
import json

# Notes
# time.time() returns microseconds


#########
# Constants (i.e. #define's) and global variables
#########

# Behavioral constants
LOOP_DELAY = 2.0   # number of seconds of delay in the main loop


# set up the callback for mqtt messages
# keep it clean and don't do any long processing here
def on_message(mqtt_client, userdata, message):
    """ handler for inbound mqtt messages """
    
    # what message did we get?
    logging.debug("MQTT message received:"+ message.topic)
    logging.debug("Raw Payload:"+ str(message.payload))

    parm = zkshop.get_parameter(message.topic)
    if parm == None:
        logging.info("Spurious topic data received ... ignored;  Topic = "+message.topic)

    # found it in the list
    else:
        logging.debug("Matched " + message.topic + " to " + parm.label)
        parm.event = True
        if parm.jflag == True:
            # convert to a string ... note: contains a 'b' before the data
            jstring = message.payload.decode('utf-8')
            jdict = json.loads(jstring)
            value = list(jdict.values())[0] # only reliable because there is only one object
        else:
            value = message.payload.decode('utf-8')

        # check the type
        # do nothing if the topic is not found in the parm list
        parm_type = type(parm.value)
        logging.debug("Setting " + message.topic + " as " + str(parm_type) + " to " + value)
        if parm_type == float:
            parm.pvalue = parm.value
            parm.value = float(value)
            
        # be careful using bool() on strings !
        elif parm_type == bool:
            if value == "True":
                parm.pvalue = parm.value
                parm.value = True
            elif value == "False":
                parm.pvalue = parm.value
                parm.value = False
            else:
                logging.error("Strange value on bool from " + message.topic)
            
        elif parm_type == int:
            parm.pvalue = parm.value
            parm.value =  int(value)
            
        elif parm_type == str:
            parm.pvalue = parm.value
            parm.value = value

        # do nothing if the topic is not found (i.e. weird type returned)


### end on_message()


### alarming effects/responses ... JUST GETTING STARTED ON THIS PART ... NOT WORKING YET

class ManageAlarms:
    """ Manage all of the automatic alarming logic """
    

    def __init__(self):
        # used to loop through the stimulus'es to be processed
        # >>> ADD NEW STIMULUSES TO BE PROCESSED HERE
        self.stimulus_list = [self.motion_detected, self.temp_hum_gas]

        # keep track of whether we are in alarming events
        self.motion_event = False

        # keep track of when the temp, humidity and gas message was sent
        self.thg_sent = False


    ### stimulus processing functions
    # >>> IF YOU ADD A NEW STIMULUS TO THE LIST (ABOVE), ADD THE HANDLER(S) HERE

    # Motion sensor : ~if auto, turn on light and send a text/mail messagae
    def motion_detected(self):
        """ process the motion detection capability """

        logging.debug("Processing motion")

        # if not in auto mode don't do this stuff: count on secure_from_auto() to cleanup
        if zkshop.auto.value == True:
            # if I am currently not in a motion event
            if self.motion_event == False:
                # motion detected?
                if zkshop.motion.value == True:
                    self.light_it_up()
                    self.send_alarm_msgs("Motion detected at " + conf["LOCATION"])
                    t = threading.Timer(conf["MOTION_HOLDOFF"], self.reset_motion_event)
                    t.daemon = True # helps with ^c behavior
                    t.start()
                    self.motion_event = True
                # end motion event    
                else:
                    self.light_it_up(True) # reset = True
        else:
            logging.debug("not auto mode ... doing nothing")


    def reset_motion_event(self):
        """ reset the motion event, usually after the timer expires """
        logging.debug("Timer resetting motion event")
        self.motion_event = False

    # Temp, Humidity, Gas value processing : send status text/mail a couple of times a day
    def temp_hum_gas(self):
        """ process the temperature, humidity and gas readings """

        if self.thg_sent == False:
            message = "T:" + str(zkshop.temp.value) + \
                      " H:" + str(zkshop.humidity.value) + \
                      " G:" + str(zkshop.gas.value)
            logging.debug("Text message: " + message)
            self.send_notif_msgs(message)
            t = threading.Timer(conf["THG_HOLDOFF"], self.reset_thg_sent)
            t.daemon = True # helps with ^c behavior
            t.start()
            self.thg_sent = True
            
    def reset_thg_sent(self):
        """ reset the temp, hum, gas "sent" flag, usually after the timer expires """
        logging.debug("Timer resetting thg sent flag")
        self.thg_sent = False


    ### processing operations to perform every so often; probalby in the main while()
        
    def process_stimuluses(self):
        """ loop through the list and process the stimuluses """
        for func in self.stimulus_list:
            func()

    def process_overrides(self):
        """ take the appropriate actions based on the override values changing """
        
        # process the light override first
        # allow the override to turn it off, knowing that it will be
        # turned back on after the holdoff if the threat continues.

        # was a new event received?
        if zkshop.o_light.event == True:
            if zkshop.o_light.value == True:
                logging.info("override commanded light on ... doing it")
                zkshop.light.value = True
            elif zkshop.o_light.value == False:
                logging.info("override commanded light off ... doing it")
                zkshop.light.value = False
            else:
                logging.error("strange value received for light override ... ignored")

            zkshop.o_light.event = False # use it only once
            

        # automatic alarming mode

        # was a new event received?
        if zkshop.o_auto.event == True:
            if zkshop.o_auto.value == True:
                logging.info("override commanded auto on ... doing it")
                zkshop.auto.value = True
            elif zkshop.o_auto.value == False:
                logging.info("override commanded auto off ... doing it")
                zkshop.auto.value = False
            else:
                logging.error("strange value received for auto override ... ignored")

            zkshop.o_auto.event = False # use it only once


    
    ### alarming responses
    # >>> IF YOU ADD A NEW ALARM OUTPUT (LIKE A SIREN), ADD THE METHOD HERE
    
    def send_alarm_msgs(self, message = "Alarm present"):
        """ send text or email messages to the configured list """

        logging.info("Begin sending alarm messages")
        for addr in conf["ALARMLIST"]:
            logging.info("Sending alarm message to: " + addr)
            # p1 piped into p2 then executed in a shell
            p1 = subprocess.Popen(["echo", message], stdout = subprocess.PIPE)
            p2 = subprocess.Popen(["mail", "-sAlarm", addr], stdin=p1.stdout, stdout = subprocess.PIPE)
            p1.stdout.close()
            output,err = p2.communicate()
            logging.error("output from mail attempt, output = " + str(output) + ";err = " + str(err))
    
    def send_notif_msgs(self, message = "Notification"):
        """ send text or email messages to the configured list """
        
        logging.info("Begin sending notification messages")
        for addr in conf["NOTIFICATIONS"]:
            logging.info("Sending notification messages message to: " + addr)
            # p1 piped into p2 then executed in a shell
            p1 = subprocess.Popen(["echo", message], stdout = subprocess.PIPE)
            p2 = subprocess.Popen(["mail", "-sNotification", addr], stdin=p1.stdout, stdout = subprocess.PIPE)
            p1.stdout.close()
            output,err = p2.communicate()
            logging.error("output from mail attempt, output = " + str(output) + ";err = " + str(err))


    def say_something(self, holdoff = 0):
        """ use the local text to voice or play a wav file """
                # say something if a motion event is active
#        if motion_event == True:
#            p1 = subprocess.Popen(["echo", "I see you"], stdout = subprocess.PIPE)
#            p2 = subprocess.Popen(["festival", "--tts"], stdin=p1.stdout, stdout = subprocess.PIPE)
#            p1.stdout.close()
#            output,err = p2.communicate()
        pass

    def make_noise(self, reset = False, holdoff = 0):
       """ activate the local siren """
       pass

    def light_it_up(self, reset = False, holdoff = 0):
        """ implement local control of the light; respect the remote override """

        # reset requested
        if reset == True:
            logging.debug("local reset of light/SSR requested")
            # check the override state
            if zkshop.o_light.value == True :
                logging.debug("... ignored")
            else:
                zkshop.light.value = False
                
        # this code is asking for the light on (e.g. alarm event) ... do it
        else:
            zkshop.light.value = True




    def secure_from_auto(self):
        """ clean things up after auto alarming is disabled """
        # end all events in progress/reset alarm event timers
        pass

    def start_auto(self):
        """ cleanly start up auto-alarming; return outputs to default """
        
        pass

        
### end of class ManageAlarms()


#########
# Setup code
#########

#
# Local actions for alarming events
# The cause() function gets called and depending on the return value
# the action() may get called.  Note that the action() may contain simple
# state machines.
#

#
# set up the debugging message level
#
# DEBUG - used
# INFO - used
# WARNING - not yet used
# ERROR - not yet used
# CRITICAL - not yet used
#

# choose one of the next two lines before deployment to send logging to a file
#logging.basicConfig(filename=conf["logfile"], level=logging.INFO, format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s')
logging.basicConfig(stream=sys.stderr,
                    level=logging.DEBUG,
                    format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s'
                    )

# announce the start
logging.info("Starting up ...")

# Instantiate MQTT
mqtt_client = mqtt.Client(conf["MQTT_CLIENT"])

#
# connect to the MQTT broker
#
logging.info("Connecting to mqtt broker ...")
mqtt_client.connect(conf["MQTT_BROKER_ADDR"], conf["MQTT_BROKER_PORT"])

# connect the message handler to the mqtt_client instance
mqtt_client.on_message = on_message

# start the thread to service mqtt traffic
mqtt_client.loop_start()

# local storage of parameters; sets up the local hardware too
zkshop = Env(logging, mqtt_client)
zkshop.data_sync(mqtt_client)

# subscribe to those which will be read
zkshop.subscribe(mqtt_client)

# Instantiate the alarm management
manage_alarms = ManageAlarms()



#############
# Main Loop
#############

logging.info("Press CTRL+C to exit")

try:
    while True :
        logging.debug("Main Loop ... Syncing data")
        zkshop.data_sync(mqtt_client)

        # process the stimuluses
        manage_alarms.process_stimuluses()

        # process overrides (mostly adjust the values in the parameter data
        manage_alarms.process_overrides()

#        zkshop.display_parameters()

        # read/write the locally hosted i/o
        zkshop.physical()


        
        # just a test
#        GPIO.output(conf["SSR_PIN"], zkshop.o_light.value)
#        logging.debug("o_light = " + str(zkshop.o_light.value))

        # Testing the SSR
#        GPIO.output(SSR_PIN, motion_event)

        # say something if a motion event is active
#        if motion_event == True:
#            p1 = subprocess.Popen(["echo", "I see you"], stdout = subprocess.PIPE)
#            p2 = subprocess.Popen(["festival", "--tts"], stdin=p1.stdout, stdout = subprocess.PIPE)
#            p1.stdout.close()
#            output,err = p2.communicate()

        time.sleep(LOOP_DELAY)

# ^C cleanup
except KeyboardInterrupt:
    logging.info("Cleaning up ... goodbye.")
    zkshop.cleanup()
    mqtt_client.loop_stop()
    mqtt_client.disconnect()
