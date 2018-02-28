#########
# Constants (i.e. #define's) and global variables
#########

conf = {
# location: used in various messages
"LOCATION" : "your location string",

# Hardware pin assignments
"MOTION_PIN" : 4,
"SSR_PIN" : 26,
"OVRLED_PIN" : 11,
"KEYSW_PIN" : 17,

# MQTT constants
"MQTT_CLIENT" : "id yourself to mosquitto",
"MQTT_BROKER_ADDR" : "your ip address as string",
"MQTT_BROKER_PORT" : <your mosquitto port as integer>,

# default name of the file to log messages
# logfile = "/var/log/Monitoring_local.log"
"LOGFILE" : "Monitoring_local.log",

# list of status notifications (comma separated list)
#"NOTIFICATIONS" : ["your first email addr", "your second email addr"],
"NOTIFICATIONS" : [],

# list of alarm notifications (comma separated list)
#"ALARMLIST" : ["your first email addr", "your second email addr"],
"ALARMLIST" : [],

# once a response to a stimulus has been activated, holdoff for this
# number of seconds before taking the action again i.e. event lasts this long
"MOTION_HOLDOFF" : 10.0,
#"T_H_G_HOLDOFF" : 86400.0,  # one day
"THG_HOLDOFF" : 25200.0,  # 7 hrs for testing
}
