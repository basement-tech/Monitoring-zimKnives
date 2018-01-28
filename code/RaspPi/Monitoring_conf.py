#########
# Constants (i.e. #define's) and global variables
#########

conf = {
# location: used in various messages
"LOCATION" : "<your location string here>",

# Hardware pin assignments
"MOTION_PIN" : 4,
"SSR_PIN" : 26,

# MQTT constants
"MQTT_CLIENT" : "<your mqtt host client name here>",
"MQTT_BROKER_ADDR" : "<ip address of broker as string>",
"MQTT_BROKER_PORT" : <broker port number as integer>,

# default name of the file to log messages
# logfile = "/var/log/Monitoring_local.log"
"LOGFILE" : "Monitoring_local.log",

# list of status notifications (comma separated list)
"NOTIFICATIONS" : [<comma sep list of email addr's as strings>],

# list of alarm notifications (comma separated list)
"ALARMLIST" : [<comma sep list of email addr's as strings>],

# once a response to a stimulus has been activated, holdoff for this
# number of seconds before taking the action again i.e. event lasts this long
"MOTION_HOLDOFF" : 30.0,
#"T_H_G_HOLDOFF" : 86400.0,  # one day
"THG_HOLDOFF" : 25200.0,  # 7 hrs for testing
}
