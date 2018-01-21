#
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

#########
# Constants (i.e. #define's) and global variables
#########

conf = {
# Hardware pin assignments
"MOTION_PIN" : 4,
"SSR_PIN" : 26,

# MQTT constants
"MQTT_CLIENT" : "your_mqtt_host_name",
"MQTT_BROKER_ADDR" : "the_addr_of_your_broker_as_string",
"MQTT_BROKER_PORT" : your_broker_port_as_integer

# default name of the file to log messages
# logfile = "/var/log/Monitoring_local.log"
"LOGFILE" : "Monitoring_local.log"
}
