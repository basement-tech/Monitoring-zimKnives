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
# Classes
###########

class Parm():
    """ class to hold a parameter and it's meta data """

    # subscribe/publish flag values
    SUB = 1
    PUB = 2

    # last changer flag values
    LOCAL = 1
    REMOTE = 2

    def __init__(self, label, value, units, when, direction, topic, acquire=lambda x: None, last_chgr=LOCAL):
        self.label = label # human readable label
        self.value = value # actual value of the parameter
        self.units = units # string version of units
        self.when = when   # timestamp for the data value
        self.direction = direction # mqtt pub or sub
        self.topic = topic # mqtt topic
        self.acquire = acquire # function used to acquire locally hosted parameters
                               # Note: some parameters are aquired by asynchronous callbacks
        self.last_chgr = last_chgr # who changed the parameter value last

class Env():
    """ describe the environmental and control parameters, and provide some convenient functions """

    # flag so that we only subscribe once
    subscribed = False
    
    def __init__(self):
        # temp, humidity, combustable gasses from the remote environmental sensor
        self.temp     = Parm("temp",     0.0,   "deg C", "00:00:00", Parm.SUB, "zk-env/temp")
        self.humidity = Parm("humidity", 0.0,   "\%",    "00:00:00", Parm.SUB, "zk-env/humidity")
        self.gas      = Parm("gas",      0.0,   "units", "00:00:00", Parm.SUB, "zk-env/gas")
        # light and auto switch override command parameters
        self.o_light  = Parm("o_light",  False, "t/f",   "00:00:00", Parm.SUB, "zk-env/o_light")
        self.o_auto   = Parm("o_auto",   False, "t/f",   "00:00:00", Parm.SUB, "zk-env/o_auto")

        # motion sensor, panic button, auto key switch hosted by the pi
        self.motion   = Parm("motion",   False, "t/f",   "00:00:00", Parm.PUB, "zk-env/motion")
        self.panicbut = Parm("panicbut", False, "t/f",   "00:00:00", Parm.PUB, "zk-env/panicbut")
        self.auto     = Parm("auto",     False, "t/f",   "00:00:00", Parm.PUB, "zk-env/auto")
        # local control of the light (usually automatic)
        self.light    = Parm("light",    False, "t/f",   "00:00:00", Parm.PUB, "zk-env/light")

        # used to loop through the parameters in other functions
        self.parm_list = [self.temp, self.humidity, self.gas, self.o_light, self.o_auto,
                          self.motion, self.panicbut, self.light, self.auto]

    def subscribe(self, broker):
        """ subscribe to those parameters indicating so ... only once """
        if self.subscribed == False:
            for attr in self.parm_list:
                if attr.direction == attr.SUB:
                    logging.debug("Subscribing: "+attr.label)
                    mqtt_client.subscribe(attr.topic)
            self.subscribed = True
        else:
            logging.debug("Already subscribed ... ignoring")
        

    def data_sync(self, broker):
        """ sync all of the local data with the data broker """

        # publish the data values that this program is sourcing
        for attr in self.parm_list:
            if attr.direction == attr.PUB:
                logging.debug("Publishing: "+attr.label)
                mqtt_client.publish(attr.topic, attr.value)

        # note that the subscribed values are updated asynchronously by on_message()
                


    def set_parameter(self, topic, value):
        """ set a single parameter value in the class attibute """

        found = False
        
        for attr in self.parm_list:
            if attr.topic == topic:
                attr.value = value
                found = True

        if found == False:
            logging.debug("Can't set value for "+topic)

        return found

    def get_topic(self, label):
        """ find the topic for the provided label """

        for attr in self.parm_list:
            if attr.label == label:
                return attr.topic

        return ""
                


                
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

class ManageAlarms():
    """ Manage all of the automatic alarming logic """

    # keep track of which was the last transition and set it appropriately
    # i.e. like a 3-way switch
    alarming_mode = False
    

    def __init__(self):
        pass
    
    def send_alarm_msgs(self):
        """ send text or email messages to the configured list """
        pass

    def say_something(self):
        """ use the local text to voice or play a wav file """
        pass

    def make_noise(self):
       """ activate the local siren """
       pass

    def light_it_up(self):
        """ turn on the lights through the SSR channel """
#        if zkshop.o_light.value 
#        zkshop.light.value = True
#        GPIO.output(SSR_PIN, zkshop.light.value)
#        logging.debug("o_light = " + str(zkshop.o_light.value))
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
zkshop = Env()
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



#############
# Main Loop
#############

logging.info("Press CTRL+C to exit")

try:
    while True :
        logging.debug("Main Loop ... Syncing data")
        zkshop.data_sync(mqtt_client)

        # just a test
        GPIO.output(conf["SSR_PIN"], zkshop.o_light.value)
        logging.debug("o_light = " + str(zkshop.o_light.value))

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
