#
# a simple program to test the condition of the link to the internet
# and reboot if necessary.
#
# ping is used to test the link to a common URL (e.g. www.google.com) a
# number of times and if failure persists, reboot the pi.
#
# it is expected to be run as su so that the shutdown command works.
#
# this file depends on the configuration file Monitoring_conf.py which
# has a dictionary defining the things that reference "conf".
#
# a breadcrumb is left in "PINGTEST_BC" so that a notification can
# be sent when the program comes back up after reboot.  Note that the
# system reboots when there is no internet, so that's why you have to 
# wait for the reboot (internet restore) to send the notification.
#
# DJZ 040118
#


import os
import time
import logging
import sys
import subprocess
from Monitoring_conf import conf

    
def send_notif_msgs(message = "Notification"):
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
logging.basicConfig(filename=conf["PINGTEST_LOGFILE"], level=logging.INFO, format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s')
#logging.basicConfig(stream=sys.stderr,
#                    level=logging.DEBUG,
#                    format='%(asctime)s - pingtest - %(levelname)s - %(message)s'
#                    )

#logging.basicConfig(stream=sys.stderr,
#                    level=logging.INFO,
#                    format='%(asctime)s - Monitoring_local - %(levelname)s - %(message)s'
#                    )

# announce the start
logging.info("Starting up pingtest ...")

#
# check if this program caused the last reboot (i.e. does the breadcrumb exist)
# if the breadcrumb exists, send a notification text per the list in the config file
# (Note: can't do this prior to rebooting (after failed pings) because there is no
#  internet connection :-)
#
if os.path.isfile(conf["PINGTEST_BC"]):
  logging.debug("breadcrumb detected ... removing it, sending notif")
  os.remove(conf["PINGTEST_BC"])
  send_notif_msgs("System was rebooted because of internet outage")
  

hostname = conf["PINGTEST_URL"]
retries = conf["PINGTEST_FAILS"]

ret_cnt = retries
while ret_cnt > 0:
  response = os.system("ping -c 1 " + hostname + " 2>&1 >/dev/null")

  #and then check the response...
  if response == 0:
    ret_cnt = retries
    logging.debug(hostname + " is up!")
  else:
    ret_cnt -= 1
    logging.warning(hostname + " is down! tries left = " + str(ret_cnt))

  time.sleep(conf["PINGTEST_INTERVAL"])
  
if conf["PINGTEST_REBOOT"] == True:
  logging.critical("system will be rebooted in 5 seconds")
  os.system("date | cat > " + conf["PINGTEST_BC"])
  time.sleep(5)
  os.system(conf["PINGTEST_RBTCMD"])
