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
# + command line arguments for logging level, filename, etc
# + implement mqtt.reconnect() logic for lost connection
# + configure the location and use to create the mqtt topics
#   ... make the data class instance name a bit more generic
# + send email on motion
# + maybe some kind of alarming action (function calls? cause:effect)
#
#

import RPi.GPIO as GPIO
import paho.mqtt.client as mqtt
import time
import subprocess
import logging
import sys
import threading
from Monitoring_conf import conf
from MonitoringParameters import *

# Notes
# time.time() returns microseconds


#########
# Constants (i.e. #define's) and global variables
#########

# Behavioral constants
LOOP_DELAY = 2.0   # number of seconds of delay in the main loop

# Global variables
light_status = False  # keep the requested status of the light

###########
# Local Classes
###########
                
###########
# Functions and callbacks
###########



# set up the callback for mqtt messages
def on_message(mqtt_client, userdata, message):
    """ handler for inbound mqtt messages """
    
    # what message did we get?
    logging.debug("MQTT message received:"+ message.topic)
    logging.debug("Raw Payload:"+ str(message.payload))
    
    # convert to a string ... note: contains a 'b' before the data
    value = message.payload.decode('utf-8')

    # is it a binary value?
    if value == "True":
        if zkshop.set_parameter(message.topic, True) == False:
            logging.warning("unhandled mqtt message received ... ignored")
    elif value == "False":
        if zkshop.set_parameter(message.topic, False) == False:
            logging.warning("unhandled mqtt message received ... ignored")
    else:
        if zkshop.set_parameter(message.topic, message.payload) == False:
            logging.warning("unhandled mqtt message received ... ignored")

### end on_message()

### functions to acquire data or respond to interrupt driven parameters

# Define callbacks for i/o changes
def motion_detected(channel):
    """ set the local motion_event flag """

    # read the input to determine which edge
    # do a little software debounce and double-check the value

    # is the detector indicating motion?
    if GPIO.input(conf["MOTION_PIN"]) == True:
        logging.debug("Rising edge detected on %s" %channel)
        
        # was the last look indicating no motion (i.e. this is a clean transition)
        if zkshop.motion.value == False:
            zkshop.motion.value = True
        # if we were already in a "motion=yes" state, must have been noise
        else:
            logging.debug("Noise")

    # edge change direction indicated : back to no motion
    else:
        logging.debug("Falling edge on %s" %channel)
        # clean transition
        if zkshop.motion.value == True:
            zkshop.motion.value = False
        # noise
        else:
            logging.debug("Noise")

### end of motion_detected()

### alarming effects/responses ... JUST GETTING STARTED ON THIS PART ... NOT WORKING YET

class ManageAlarms:
    """ Manage all of the automatic alarming logic """

    # keep track of which was the last transition and set it appropriately
    # i.e. like a 3-way switch
    alarming_mode = False
    

    def __init__(self):
        # used to loop through the stimulus'es to be processed
        self.stimulus_list = [self.motion_detected]


    ### stimulus processing

    def motion_detected(self):
        """ process the motion detection capability """

        logging.debug("Processing motion")
        
        if zkshop.motion.value == True:
            self.light_it_up()
        else:
            self.light_it_up(True)



    def process_stimuluses(self):
        """ loop through the list and process the stimuluses """
        for func in self.stimulus_list:
            func()

    def process_overrides(self):
        """ take the appropriate actions based on the override values """
        # use the zkshop.ovrd_list[]
        pass
    
    ### alarming responses
    
    def send_alarm_msgs(self, holdoff = 0):
        """ send text or email messages to the configured list """
        pass

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

        logging.debug("setting light/SSR to: " + str(zkshop.light.value))
        GPIO.output(conf["SSR_PIN"], zkshop.light.value)


    def secure_from_auto(self):
        """ clean things up after auto alarming is disabled """
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

# local storage of parameters
zkshop = Env(logging, mqtt_client)
zkshop.data_sync(mqtt_client)

# subscribe to those which will be read
zkshop.subscribe(mqtt_client)

# BCM numbering scheme for Pi pins
GPIO.setmode(GPIO.BCM)

#
# setup the motion control input
#
GPIO.setup(conf["MOTION_PIN"], GPIO.IN)
GPIO.setup(conf["SSR_PIN"], GPIO.OUT)
GPIO.output(conf["SSR_PIN"], False)

#
# There seems to be a bug where the edge detection triggers on both
# edges.  Compensate in the ISR.
#
GPIO.add_event_detect(conf["MOTION_PIN"], GPIO.BOTH, callback=motion_detected)

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
    GPIO.cleanup()
    mqtt_client.loop_stop()
    mqtt_client.disconnect()
