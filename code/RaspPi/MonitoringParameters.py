# MonitoringParameters.py
#
# Organize the data for the Monitoring_zimKnives project.
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
# This project uses an mqtt data broker.  Some parameters are "published"
# to the broker from a remote unit.  This python code subscribes to them.
# Other sensors are hosted directly on the Pi on which this code runs.
# This python code publishes those values to the data broker.
#
# Note that part of this design expects NodeRed (graphical display and setting
# of values on the broker) to display and/or remotely/manually control
# things in this system.
#
# Local copies of the data on the broker are maintained here.
#
# Note: some of the parent file is hardcoded to the parameters that
# Env() sets up.
#

import RPi.GPIO as GPIO
from Monitoring_conf import conf

class Parm:
    """ class to hold a parameter and it's meta data """

    # subscribe/publish flag values
    SUB = 1
    PUB = 2

    # last changer flag values
    LOCAL = 1
    REMOTE = 2

    def __init__(self, label, value, pvalue, units, when, direction, topic, event = False, jflag = False, physical=lambda x: None, io_pin = -1, io_dir = GPIO.IN):
        self.label = label # human readable label
        self.value = value # actual value of the parameter
        self.pvalue = pvalue # previous value; used for s/w edge detection
        self.units = units # string version of units
        self.when = when   # timestamp for the data value
        self.direction = direction # mqtt pub or sub
        self.topic = topic # mqtt topic
        self.event = event # was an asynchronous event received
        self.jflag = jflag # decode incoming as json or not
        self.physical = physical # function used to acquire/set locally hosted parameters
                               # Note: some parameters are aquired by asynchronous callbacks
        self.io_pin = io_pin # in the case of physical i/o
        self.io_dir = io_dir # for physical i/o is the pin in or out


class Env:
    """ describe the environmental and control parameters, and provide some convenient functions """
    
    # flag so that we only subscribe once
    subscribed = False
    
    def __init__(self, logging, mqtt_client):
        self.logging = logging
        self.mqtt_client = mqtt_client

        #
        # >>> ADD NEW PARAMETERS HERE ... ADD HANDLERS DOWN BELOW
        #
        # Note: pvalue may not be maintained for all parameters
        #       (definitely not for o_auto and o_light)
        #
        #       only o_auto and o_light reset the event flag
        #
        #                    label       value  pvalue  units     when      direction  topic             event   jflag   physical       pin
        #                                                                                               (false) (false)   (None)        (-1)
        #                    -----       -----  ------  -----     ----      ---------  -----             -----  -----    --------       ---
        # temp, humidity, combustable gasses from the remote environmental sensor
        self.temp     = Parm("temp",     0.0,   0.0,   "deg C", "00:00:00", Parm.SUB, "zk-env/temp",     False,  True)
        self.humidity = Parm("humidity", 0.0,   0.0,   "\%",    "00:00:00", Parm.SUB, "zk-env/humidity", False,  True)
        self.gas      = Parm("gas",      0.0,   0.0,   "units", "00:00:00", Parm.SUB, "zk-env/gas",      False,  True)
        # light and auto switch override command parameters
        self.o_light  = Parm("o_light",  False, False, "t/f",   "00:00:00", Parm.SUB, "zk-env/o_light")
        self.o_auto   = Parm("o_auto",   False, False, "t/f",   "00:00:00", Parm.SUB, "zk-env/o_auto")

        # motion sensor, panic button, auto key switch hosted by the pi
        self.motion   = Parm("motion",   False, False, "t/f",   "00:00:00", Parm.PUB, "zk-env/motion",   False, False, lambda x: None, conf["MOTION_PIN"], GPIO.IN)
        self.panicbut = Parm("panicbut", False, False, "t/f",   "00:00:00", Parm.PUB, "zk-env/panicbut")
        self.auto     = Parm("auto",     False, False, "t/f",   "00:00:00", Parm.PUB, "zk-env/auto")
        self.keysw    = Parm("keysw",    False, False, "t/f",   "00:00:00", Parm.PUB, "zk-enf/keysw",    False, False, self.read_pin, conf["KEYSW_PIN"], GPIO.IN)

        # local control of the light (usually automatic)
        self.light    = Parm("light",    False, False, "t/f",   "00:00:00", Parm.PUB, "zk-env/light",    False, False, self.write_pin, conf["SSR_PIN"], GPIO.OUT)

        # automatic override indicator
        self.ovrled   = Parm("ovrled",   False, False, "t/f",   "00:00:00", Parm.PUB, "zk-env/ovrled",   False, False, self.write_pin, conf["OVRLED_PIN"], GPIO.OUT)

        # used to loop through the parameters in other functions
        self.parm_list = [self.temp, self.humidity, self.gas, self.o_light, self.o_auto, self.keysw,
                          self.motion, self.panicbut, self.light, self.auto, self.ovrled]





### this section has the code to manipulate the parameter data structure
### and interact with the MQTT data broker
### (code for physical i/o below)
        
    def subscribe(self, broker):
        """ subscribe to those parameters indicating so ... only once """
        if self.subscribed == False:
            for attr in self.parm_list:
                if attr.direction == attr.SUB:
                    self.logging.debug("Subscribing: "+attr.label)
                    self.mqtt_client.subscribe(attr.topic)
            self.subscribed = True
        else:
            self.logging.debug("Already subscribed ... ignoring")
        

    def data_sync(self, broker):
        """ sync all of the local data with the data broker """

        # publish the data values that this program is sourcing
        for attr in self.parm_list:
            if attr.direction == attr.PUB:
                self.logging.debug("From data_sync() " + "Publishing: "+attr.label)
                self.mqtt_client.publish(attr.topic, attr.value)

        # note that the subscribed values are updated asynchronously by on_message()

    def get_parameter(self, topic):
        """ get the parameter instance for the provided topic """
                
        for attr in self.parm_list:
            if attr.topic == topic:
                return attr

        self.logging.error("Can't find topic: "+topic)
        return None
    
    def get_parameter_type(self, topic):
        """ get the type of the parameter by topic  """
        
        for attr in self.parm_list:
            if attr.topic == topic:
                return type(attr.value)

        self.logging.error("Can't find topic: "+topic)
        return type(None)


    def set_parameter(self, topic, value):
        """ set a single parameter value in the class attibute
            return False if topic not found
        """

        found = False
        
        for attr in self.parm_list:
            if attr.topic == topic:
                attr.value = value
                found = True

        if found == False:
            self.logging.debug("Can't set value for "+topic)

        return found

    def get_topic(self, label):
        """ find the topic for the provided label """

        for attr in self.parm_list:
            if attr.label == label:
                return attr.topic

        return ""

    def display_parameters(self):
        """ for debugging, display all parameter values """

        self.logging.debug("============")
        for attr in self.parm_list:
            self.logging.debug(attr.label + " = " + str(attr.value))
        self.logging.debug("============")

            
### below is the stuff to handle the physical i/o hosted on the pi
### functions to acquire data or respond to interrupt driven parameters
#
# >>> ADD YOUR HANDLERS FOR THE INDIVIDUAL PARAMETERS HERE
#

    def motion_edge(self, channel):
        """ set the local motion_event flag based on edge detected """

        # read the input to determine which edge
        # do a little software debounce and double-check the value

        # is the detector indicating motion?
        if GPIO.input(self.motion.io_pin) == True:
            self.logging.debug("Rising edge detected on %s" %channel)
            
            # was the last look indicating no motion (i.e. this is a clean transition)
            if self.motion.value == False:
                self.motion.pvalue = self.motion.value
                self.motion.value = True
            # if we were already in a "motion=yes" state, must have been noise
            else:
                self.logging.debug("Noise")

        # edge change direction indicated : back to no motion
        else:
            self.logging.debug("Falling edge on %s" %channel)
            # clean transition
            if self.motion.value == True:
                self.motion.pvalue = self.motion.value
                self.motion.value = False
            # noise
            else:
                self.logging.debug("Noise")

    ### end of motion_edge()
                
    def physical_init(self):
        """ initialize the physical i/o lines """
        
        # BCM numbering scheme for Pi pins
        GPIO.setmode(GPIO.BCM)
        
        for attr in self.parm_list:
            if attr.io_pin > 0:
                GPIO.setup(attr.io_pin, attr.io_dir)
                if attr.io_dir == GPIO.OUT:
                    GPIO.output(attr.io_pin, attr.value)
        #
        # There seems to be a bug where the edge detection triggers on both
        # edges.  Compensate in the ISR.
        #
        GPIO.add_event_detect(self.motion.io_pin, GPIO.BOTH, callback=self.motion_edge)
        
            
    def physical(self):
        """ execute the functions to interact with physical i/o lines """

        for attr in self.parm_list:
            attr.physical(attr)


    def write_pin(self, attr):
        """ output a value of the parameter to a physical i/o pin """
        
        self.logging.debug("Setting " + attr.label + " to " + str(attr.value) + " on pin " + str(attr.io_pin))
        GPIO.output(attr.io_pin, attr.value)

    def read_pin(self, attr):
        """ input a value of the parameter to a physical i/o pin """
        
        self.logging.debug("Setting " + attr.label + " to " + str(attr.value) + " from pin " + str(attr.io_pin))
        attr.pvalue = attr.value
        attr.value = bool(GPIO.input(attr.io_pin))
        if attr.value != attr.pvalue:
            attr.event = True

    def cleanup(self):
        GPIO.cleanup()



                
