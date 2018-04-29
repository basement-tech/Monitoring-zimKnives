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

# used by separate ping test program to make sure the internet is working
"PINGTEST_URL" : "www.google.com",
"PINGTEST_FAILS" : 3,
"PINGTEST_INTERVAL" : 600, # time in seconds between ping attempts
"PINGTEST_RBTCMD" : "shutdown -r now",
#"PINGTEST_RBTCMD" : "echo fake reboot",
"PINGTEST_REBOOT" : True,  # False to just log the status; True to execute the RBTCMD
"PINGTEST_BC" : "/home/pi/develop/Monitoring_reboot", # breadcrumb if reboot happens
"PINGTEST_LOGFILE" : "/var/log/pingtest.log",

# MQTT constants
"MQTT_CLIENT" : "id yourself to mosquitto",
"MQTT_BROKER_ADDR" : "your ip address as string",
"MQTT_BROKER_PORT" : <your mosquitto port as integer>,

# default name of the file to log messages
"LOGFILE" : "/var/log/Monitoring_local.log",
#"LOGFILE" : "Monitoring_local.log",

# list of status notifications (comma separated list)
#"NOTIFICATIONS" : ["your first email addr", "your second email addr"],
"NOTIFICATIONS" : [],

# list of alarm notifications (comma separated list)
#"ALARMLIST" : ["your first email addr", "your second email addr"],
"ALARMLIST" : [],

# once a response to a stimulus has been activated, holdoff for this
# number of seconds before taking the action again i.e. event lasts this long
"MOTION_HOLDOFF" : 300.0,
#"THG_HOLDOFF" : 86400.0,  # one day
"THG_HOLDOFF" : 43200.0,  # 12 hrs
#"THG_HOLDOFF" : 300.0,  # 5 mins for testing
"LIM_HOLDOFF" : 300.0,

# *****
# *** The formula's behind these limits are a guess !!!  *** NOT CALIBRATED ***
# *** Under absolutely no circumstances should this software/firmware alone or in
# *** combination with hardware on which it was designed to run be used 
# *** to make safety and/or health decisions.
# ***
# *** Do not use it to determine if a
# *** location is safe for human or animal occupancy.  It was developed solely
# *** for hobby and learning purposes.
# *** DO YOUR OWN HOMEWORK ... FIND YOUR OWN EXPERTS.
# *****

# These entries set the limits for parameter values
# Add as many as is required ... don't forget to increment the line numbers
"LIMIT_CHECKS" : {
    "1": {"parm": "gasco", "limit": 15.0, "sense": "high", "message": "NOTIF: Carbon Monoxide is above normal"},
    "2": {"parm": "gasco", "limit": 35.0, "sense": "high", "message": "ALERT: Carbon Monoxide is very high"},
    "3": {"parm": "gaspr", "limit": 1000.0, "sense": "high", "message": "NOTIF: Propane reading is above normal"},
    "4": {"parm": "gaspr", "limit": 5000.0, "sense": "high", "message": "ALERT: Propane reading is very high"},
    "5": {"parm": "temp",  "limit": 2.0,  "sense": "low",  "message": "ALERT: Temp is very low"},
    "6": {"parm": "temp",  "limit": 40.0, "sense": "high",  "message": "ALERT: Temp is very high"},
    }

}
