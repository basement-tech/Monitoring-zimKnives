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


class Parm:
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

class Env:
    """ describe the environmental and control parameters, and provide some convenient functions """
    
    # flag so that we only subscribe once
    subscribed = False
    
    def __init__(self, logging, mqtt_client):
        self.logging = logging
        self.mqtt_client = mqtt_client
        
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
                self.logging.debug("Publishing: "+attr.label)
                self.mqtt_client.publish(attr.topic, attr.value)

        # note that the subscribed values are updated asynchronously by on_message()
                


    def set_parameter(self, topic, value):
        """ set a single parameter value in the class attibute """

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
                


                
