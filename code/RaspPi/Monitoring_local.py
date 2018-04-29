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
# Links that I found useful:
# python3 reference:
#  https://docs.python.org/3/reference/index.html
# mqtt:
#  https://pypi.python.org/pypi/paho-mqtt/1.1
# Threading timers:
#  https://docs.python.org/2.4/lib/timer-objects.html
# i/o with R Pi pins:
#  https://sourceforge.net/p/raspberry-gpio-python/wiki/BasicUsage/
#  https://sourceforge.net/p/raspberry-gpio-python/wiki/Inputs/
#
# v1.0
# + replaced gas parameter with three to support raw and PPM data from the sensor
#   (changed in MonitoringParameters.py)
# + adjusted temp_hum_gas() response to include the new gas parameters
# + accomodated the change in the mqtt/json packet from the remote sensor
#   (added setting the timestamp per parameter from the remote sensor packet)
# + implemented limit alarms, primarily for environmental/numerical parameters
#   (modified Monitoring_conf.py to specify the timing holdoff and limit values)
#
# v0.9
# + fixed bug where secure_from_auto() was resetting the t/g/h timer
#   (t/g/h notification is not controlled by auto mode)
#
#
# v0.8 (i don't like the number in between)
# + moved the t/h/g timer to the new local timer class
# + added the override led to the parameters class and tested
# + added setting of parm.event on MQTT-based, bool parameters
# + rearranged the initialization of the local hardware
#   (added i/o flag and initialization method to Parm class ... much cleaner)
# + added some timer logic to allow clean cleanup
# + added handling of the remote auto and key switch auto together
# + implemented logic to control the override led appropriately
# 
#
# v0.6:
# + created timer class for reusability and auto-False cleanup
# + added on_disconnect, callback for mqtt broker: attempt reconnect in main loop
# + added a mqtt_con_status and use it to try to reconnect if connection is lost
# + had to add catching of ConnectionRefusedError exception in order to keep things moving
#    on MQTT conneciton attempt (testing by first killing mosquitto data broker) ... cool !
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
# + add remote reboot capability
# + add thresholds for t/h/g to change from notifications to alarm
# + command line arguments for logging level, filename, etc
# + configure the location and use to create the mqtt topics
#   ... make the data class instance name a bit more generic
# + reboot on ethernet/network loss ... seems to happen
# + add timer and configuration parameters to allow leaving after setting auto
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

# keep track of the mqtt broker connection status
mqtt_con_status = False
MQTT_ERR_SUCCESS = 0

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
            value = jdict[list(jdict)[0]]["value"] # only reliable because there is only one object
            parm.when = jdict[list(jdict)[0]]["tstamp"]
        else:
            value = message.payload.decode('utf-8')

        # check the type
        # do nothing if the topic is not found in the parm list
        parm_type = type(parm.value)
        logging.debug("Setting " + message.topic + " as " + str(parm_type) + " to " + str(value))
        if parm_type == float:
            parm.pvalue = parm.value
            parm.value = float(value)
            
        # be careful using bool() on strings !
        elif parm_type == bool:
            if value == "True":
                if parm.pvalue == False:
                    parm.event = True
                parm.pvalue = parm.value
                parm.value = True
            elif value == "False":
                if parm.pvalue == True:
                    parm.event = True
                parm.pvalue = parm.value
                parm.value = False
            else:
                logging.error("on_message():Strange value on bool from " + message.topic)
            
        elif parm_type == int:
            parm.pvalue = parm.value
            parm.value =  int(value)
            
        elif parm_type == str:
            parm.pvalue = parm.value
            parm.value = value

        # do nothing if the topic is not found (i.e. weird type returned)


### end on_message()

# callbacks for the mqtt databroker interaction
def on_disconnect(client, userdata, rc):
    """ callback for the mqtt data broker disconnect """

    global mqtt_con_status
    # remember, this is a disconnect status
    if rc == MQTT_ERR_SUCCESS:
        mqtt_con_status = False
        logging.info("Clean MTT disconnece")
    else:
        logging.error("Unexpected disconnect from mqtt data broker")


def on_connect(client, userdata, flags, rc):
    """ callback for mqtt data broker connect """

    global mqtt_con_status
    
    if rc == MQTT_ERR_SUCCESS:
        logging.info("MQTT connect success")
        mqtt_con_status = True
    else:
        logging.error("Error connecting to MQTT broker")



# a little class to manage a single, global timer
class LocalTimer:
    """ make the timer available more globally and terminate easier """

    started = False
    
    def __init__(self, interval, function, args=[], kwargs={}):
        self.interval = interval
        self.function = function
        self.args = args
        self.kwargs = kwargs

    def create(self):
        """ arm a timer ... need to call start() to start ... self cancelling """
        self.timer = threading.Timer(self.interval, self.function, self.args, self.kwargs)
        self.timer.daemon = True  # helps with ^c behavior

    def start(self):
        """ start a timer that has beed created using create() """
        self.timer.start()
        self.started = True

    def cancel(self):
        """ cancel a timer that has yet to complete """
        self.timer.cancel()
        self.started = False
        

class ManageAlarms:
    """ Manage all of the automatic alarming logic """
    

    def __init__(self):
        # used to loop through the stimulus'es to be processed
        # >>> ADD NEW STIMULUSES TO BE PROCESSED HERE
        self.stimulus_list = [self.motion_detected, self.temp_hum_gas, self.auto_on_off, self.set_ovrled]

        # keep track of whether we are in alarming events
        self.motion_event = False

        # keep track of when the temp, humidity and gas status message was sent
        self.thg_sent = False

        # keep track of when the limit message has been sent
        self.limit_sent = False

        # instantiate the local timer class to be used over and over
        self.mtimer = LocalTimer(conf["MOTION_HOLDOFF"], self.reset_motion_event)
        self.ttimer = LocalTimer(conf["THG_HOLDOFF"], self.reset_thg_sent)
        self.ltimer = LocalTimer(conf["LIM_HOLDOFF"], self.reset_limit_sent)


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
                    self.mtimer.create()
                    self.mtimer.start()
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
        self.mtimer.started = False

    # Temp, Humidity, Gas value processing : send status text/mail a couple of times a day
    def temp_hum_gas(self):
        """ process the temperature, humidity and gas readings.
            not controlled by auto mode (i.e. runs continuously) """

        if self.thg_sent == False:
            message = "T:" + str(zkshop.temp.value) + \
                      " H:" + str(zkshop.humidity.value) + \
                      " G_CO:" + str(zkshop.gasco.value) + \
                      " G_PR:" + str(zkshop.gaspr.value)
            logging.debug("Text message: " + message)
            self.send_notif_msgs(message)
            self.ttimer.create()
            self.ttimer.start()
            self.thg_sent = True
            
    def reset_thg_sent(self):
        """ reset the temp, hum, gas "sent" flag, usually after the timer expires """
        logging.debug("Timer resetting thg sent flag")
        self.thg_sent = False
        self.ttimer.started = False

    def reset_limit_sent(self):
        """ reset the limit sent flag, usually after the timer expires """
        logging.debug("Timer resetting limit sent flag")
        self.limit_sent = False
        self.ltimer.started = False

    # auto mode on/off processing
    def auto_on_off(self):
        """ do, sort of a three-way switch with remote and local key switch for auto mode """
        
        # was a new event from the key received?
        if zkshop.keysw.event == True:
            if zkshop.keysw.value == True:
                logging.info("keysw commanded auto on ... doing it")
                zkshop.auto.value = True
            elif zkshop.keysw.value == False:
                logging.info("keysw commanded auto off ... doing it")
                zkshop.auto.value = False
                self.secure_from_auto()
            else:
                logging.error("strange value received for auto override ... ignored")

            zkshop.keysw.event = False # use it only once

        if zkshop.o_auto.event == True:
            if zkshop.o_auto.value == True:
                logging.info("override commanded auto on ... doing it")
                zkshop.auto.value = True
            elif zkshop.o_auto.value == False:
                logging.info("override commanded auto off ... doing it")
                zkshop.auto.value = False
                self.secure_from_auto()
            else:
                logging.error("strange value received for auto override ... ignored")

            zkshop.o_auto.event = False # use it only once

    def set_ovrled(self):
        """ adjust the state of the auto override led """

        zkshop.ovrled.value = zkshop.keysw.value ^ zkshop.auto.value
            

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
        # code moved to stimulus section because it is combined with key switch input

    def process_limits(self):
        """ loop through the "LIMIT_CHECKS" dictionary from the conf file and
            take send messages if warranted """

        logging.debug("Processing limits ...")

        message = "LIMIT"

        # loop through all of the limit checks from the config file
        # build up a single message to be sent after all are processed
        for item in conf["LIMIT_CHECKS"]:
                parm = zkshop.get_parameter_by_label(conf["LIMIT_CHECKS"][item]["parm"])
                if parm == None:
                    logging.info("Spurious label in LIMIT_CHECKS... ignored;  label = " + conf["LIMIT_CHECKS"][item]["parm"])
                else:
                    if conf["LIMIT_CHECKS"][item]["sense"] == "high":
                        if parm.value >= conf["LIMIT_CHECKS"][item]["limit"]:
                            logging.info("Limit message: " + conf["LIMIT_CHECKS"][item]["message"] + \
                            " (parm:" + conf["LIMIT_CHECKS"][item]["parm"] + \
                            " limit:" + str(conf["LIMIT_CHECKS"][item]["limit"]) + \
                            " value: " + str(parm.value) + " " + parm.units)

                            message = message + "\n" + conf["LIMIT_CHECKS"][item]["message"]

                    elif conf["LIMIT_CHECKS"][item]["sense"] == "low":
                        if parm.value <= conf["LIMIT_CHECKS"][item]["limit"]:
                            logging.info("Limit message: " + conf["LIMIT_CHECKS"][item]["message"] + \
                            " (parm:" + conf["LIMIT_CHECKS"][item]["parm"] + \
                            " limit:" + str(conf["LIMIT_CHECKS"][item]["limit"]) + \
                            " value: " + str(parm.value) + " " + parm.units)

                            message = message + "\n" + conf["LIMIT_CHECKS"][item]["message"]
                            
                    else:
                        logging.info("Bad sense in LIMIT_CHECKS ... ignored; sense = " + conf["LIMIT_CHECKS"][item]["sense"])

        # if a message was created (i.e. a limit was exceeded), send it
        if message != "LIMIT":            
            # if a limit was exceeded and a message has not been sent
            # recently (controlled by "LIM_HOLDOFF"), create and send it
            if self.limit_sent == False:
                logging.debug("Text message: " + message)
                self.send_alarm_msgs(message)
                self.ltimer.create()
                self.ltimer.start()
                self.limit_sent = True


    
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
        self.motion_event = False
        if self.mtimer.started == True:
            self.mtimer.cancel()


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
logging.basicConfig(filename=conf["LOGFILE"], level=logging.INFO, format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s')
#logging.basicConfig(stream=sys.stderr,
#                    level=logging.DEBUG,
#                    format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s'
#                    )

#logging.basicConfig(stream=sys.stderr,
#                    level=logging.INFO,
#                    format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s'
#                    )

# announce the start
logging.info("Starting up ...")

# Instantiate MQTT
mqtt_client = mqtt.Client(conf["MQTT_CLIENT"])

#
# connect to the MQTT broker
#
logging.info("Connecting to mqtt broker ...")
try:
    mqtt_client.connect(conf["MQTT_BROKER_ADDR"], conf["MQTT_BROKER_PORT"])
except ConnectionRefusedError:
    logging.error("MQTT connection error on first attempt")

# connect the message handlers to the mqtt_client instance
mqtt_client.on_message = on_message
mqtt_client.on_disconnect = on_disconnect
mqtt_client.on_connect = on_connect

# start the thread to service mqtt traffic
mqtt_client.loop_start()

# local storage of parameters; sets up the local hardware too
zkshop = Env(logging, mqtt_client)
zkshop.data_sync(mqtt_client)

# initialize the locally connected hardware
zkshop.physical_init()

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
        
        # read/write the locally hosted i/o
        zkshop.physical()

        zkshop.display_parameters()

        manage_alarms.process_limits()

        if mqtt_con_status == False:
            try:
                mqtt_client.reconnect()
            except ConnectionRefusedError:
                logging.debug("MQTT connection error on reconnect attempt")

        time.sleep(LOOP_DELAY)

# ^C cleanup
except KeyboardInterrupt:
    logging.info("Cleaning up ... goodbye.")
    manage_alarms.secure_from_auto()
    zkshop.cleanup()
    mqtt_client.loop_stop()
    mqtt_client.disconnect()
