/**************************************
 * WARNING : this is the beginning of a new major version
 * where much of the generally useful code is moved to arduino libraries
 * 
 * AT THIS POINT, IT COMPILES AND WAS TESTED WITH A SIMPLE USECASE !!! (USE VERSION 1.6 FOR NOW)
 * 
 * UPDATE: testing with the GARAGE_REM configuration and the door sensor and SSR
 * function as expected.
 * 
 ***************************************/


/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of these words as published by the author.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  Use at your own risk.
 * 
 * Under absolute no circumstances should this software/firmware alone or in
 * combination with hardware on which it was designed to run be used 
 * to make safety and/or health decisions.  Do not use it to determine if a
 * location is safe for human or animal occupancy.  It was developed solely
 * for hobby and learning purposes.
 * 
 * RemoteEnvSensorESP
 * Daniel J. Zimmerman
 * 12/06/17 - 9/2021
 * 
 ****************************************************************************
 * Firmware to acquire environmental parameters and send via mqtt to broker *
 ****************************************************************************
 * Introduction:
 * 
 * This software/firmware was/is being developed under the Arduino IDE and is being hosted on the
 * Adafruit Huzzah ESP8266.  It should run on other host hardware; it does, however, expect the
 * ESP8266 WiFi hardware.
 * 
 * Using the other Adafruit modules listed below, it acquires environmental parameters and primarily
 * sends them via json/mqtt/WiFi to a mosquito data broker, at this writing hosted on a
 * Raspberry Pi (although any mosquito data broker will do).  The Raspberry Pi also hosts Node Red,
 * a public domain, graphical data visualization and control package.
 * 
 * Although originally written to monitor
 * a remote shop that used a propane fired forge (temp, humidity, CO & Propane concentration), it
 * has since been expanded to monitor a CNC controller, adding additional temperature capability
 * using thermistors (for heat sinks) and electrical current measurement using a shunt embedded 
 * in the controller.  Additional data visualization has been added with the capability to drive
 * a strip of neoPixels (30 at this writing), also from Adafruit.  Finally, control of a solid state
 * relay using mqtt parameters was added.  This required reading (subscribing) as well as writing (publishing)
 * data from/to the broker.
 * 
 * Extensive parameterization is provided allowing a large number of module combinations to be hosted.
 * 
 * ****************************************************************************************************
 * 
 * Hardware:
 * Adafruit Huzzah ESP8266 module (for development; ESP-12S (AF P2491) target)
 * Adafruit HTU21D Temp/Humidity i2c module - The HTU21D-F has a default i2c address of 0x40 and cannot be changed!
 * Adafruit ADS1015 12-bit i2c ADC (P1083) i2c address range: 0x48-0x4B (Host of thermistors and current sensing)
 * Adafruit MiCS5524 Gas Sensor (P3199)
 * The below current sensors output analog voltages and interface through the ADS1015:
 *   Adafruit INA169 Analog DC Current Sensor Breakout - 60V 5A Max (P1164)
 *   Allegro Micro Systems ACS758LCB-050U-PFF-T Hall Effect Current Sensor - 50A
 * Adafruit BME680 (P3660) Temp, Humidity, Pressure, VOC - By default, the i2c address is 0x77
 * Solid State Relay supported by gpio
 * MCP23017 i2c based, 16 additional i/o lines - default i2c address of 0x20, can be changed
 * ThingPulse ESP8266 WiFi Color Display Kit 2.4 
 *   (NEED TO USE "LOLIN(WEMOS) D1 R2 & mini" BOARD in Arduino IDE 
 *    i.e. ARDUINO_ESP8266_WEMOS_D1MINI is #defined by IDE)
 * 
 * 
 * Boards and Libraries:
 * - In Arduino IDE->File->Preferences, add this to the "Additional Board Managaer URLs" field:
 * http://arduino.esp8266.com/stable/package_esp8266com_index.json
 * - Go to the Tools->Board->Board Manager ... (at the top of the board selection list)
 * - Install "esp8266 by ESP8266 Community"
 * 
 * For Thingpulse Color Display V2 use LOLIN(WEMOS) D1 R2 & mini
 * 
 * In Tools->Manage Libraries ... :
 * - Install the Adafruit ADS1X15 by Adafruit
 * - Install Adafruit HTU21DF by Adafruit
 * - InstallPubSub Client by Nick O'Leary
 * - Install NTP Client by Fabrice Weinberg
 * - Adafruit Neopixel library
 * - Adafruit BME680 library
 * - Adafruit MCP23017 library
 * - Local Libraries:
 *   bt_eepromlib by basement tech/zimTech
 *   bt_mqttlib by basement tech/zimTech
 * 
 * I was building under IDE 1.6.7.  Upgraded to 1.8.15 and updated some libraries.
 * 
 * ***************************************************************************************************
 * This file is organized according to this map (the capitalized words are found literally below):
 * 
 * Top: This comment section
 * Support functions and #defines:
 *   HARDWARE    (pins and what modules are present)
 *   EEPROM      (for storing persistent data like WIFI credentials)
 *   WIFI        (ssid/password)
 *   MQTT        (topics and mosquito location)
 *   BEHAVIORAL  (timing, retrys, etc )
 *   ADS1015     (adc setup, etc.)
 *   THERMISTOR  (formulas, etc.)
 *   INA169      (high side current monitor formulas, etc.)
 *   ACS758      (hall effect current sensor; mutually exclusive to INA169 (i.e. pick one)
 *   MiCS5524    (gas sensor formulas, etc.)
 *   NEOPIXELS   (neopixel characteristics,  modes, etc.)
 *   NIST        (network time functions)
 *   HTU21D      (temp and humidity sensor formulas, etc.)
 *   MQTT        (mosquito dialog and json processing)
 *   LOCAL_SSR   (solid state relay control)
 *   GDOOR_SENSE (garage door sense switch)
 *   BME680      (temp, humidity, pressure, VOC)
 *   MCP23017    (additional i/o capability)
 * 
 * SETUP:
 *   Allow for editing of the EEPROM parameters
 *   Setup all of the hardware
 *   Sync the default neopixel mode and range values with mosquitto/nodered
 *   Initialize the parameter structures
 *   
 * LOOP:
 *   Fast acquisition loop (currently requiring 12-15 mS to execute)
 *     read the momentary contact switch,
 *        remember if yes and adjust the neopixel mode in the slow loop
 *     ATD (or A/D if you prefer) acquitision
 *     calculate running average of atd readings
 *     thermistor conversion to degrees (adc channels)
 *     INA169: convert to amps OR ACS758 convert to amps
 *     Neopixels: adjust led's based on display mode
 *     read the estop input bit
 *     execute mqtt.loop() to check for incoming mqtt
 *     
 *   Slow acquisition loop
 *     adjust the neopixel display mode and gain from mqtt values
 *     Status the WIFI and mqtt, retry connect if necessary
 *     read the temp/humidity from the HTU21D (slower changing parameters)
 *     read the temp/hum/etc. from the BME680
 *     publish all of the environmental data via mqtt
 *     
 *   minicom notes:
 *     I use minicom to interact with the board when I'm not writing
 *     software in the Arduino IDE. I have had mixed results using the
 *     Arduino IDE for serial port i/o.
 *     ***IMPORTANT==> You must disable hardware flow control in minicom or you'll
 *     pull your hair out wondering why you cant get keyboard input to be recognized.
 *     Also, it's hiding in the ^A Z, cOnfigure Minicom ..O, Serial port setup submenu.
 *     I used a speed of 115200 with no issue.  Use that same speed in the Serial.begin() call.
 *     
 *   Starting to implement local DEBUG messaging levels ... something like this:
 *   
 *   0 : only UI, startup and significant operational things
 *   1 : 
 *   2 : MQTT traffic
 *   3 : 
 *   4 : 
 *   5 : Local sample data
 *   6 : 
 *   7 : 
 *   8 :
 *   9 : every level of detail; beware of wierd timing things
 *     
 *******************************************************************************************************
 * 
 * *****************************************************************************************************
 * Change history:
 * Pending:
 *
 * Next (* = PRIORITY):
 * o potentially move the HTU21D read of temp and humidity to the slow loop
 * o blink the WIFI connected LED for heartbeat
 * o if the NIST time sync fails, annotate the time with an asterisk
 * o implement a red "failed" light
 * o add the ability to reboot remotely from a data broker topic flag; maybe heartbeat
 * o Test out the INA169 path again after the ACS758 edits
 * o add a neopixel_mode that displays temps and humidity (for WIFI_ON off)
 * o add a neopixel_mode to display heatsink temps
 * * add a "worklight" neopixel mode
 * o for fun, add a neopixel chase mode to show off
 * o include the topics in the thermistor structure
 * o connect the thermistor structure to the EEPROM values
 * o finish ACS758_set_offset()
 * 
 * v2.1
 * + added support for thing_pulse display
 *   ... tested with garage topics published by another device
 *   (displays a 2nd page of subscribed topics)
 * o modified definition of struct parameters in bt_mqttlib.h to
 *   accommodate a display label
 *   NO LONGER COMPATIBLE WITH LESSER VERSIONS OF THIS CODE (<=2.0)
 * o added/updated/fixed support for ":" in json string
 * o using ARDUINO_ESP8266_WEMOS_D1MINI to decide whether to build graphics display functions
 *   ( to avoid "D1 not defined in this context" errors when building for non-graphics version)
 * o added flag to determine whether the timestamp should be sent
 *   (for display only devices which do not collect data, don't want to publish the timestamp)
 * o added #defines for the sense of WIFI LED since all hardware was not built the same
 *   (tested on ENV only at this point)
 * 
 * v2.0
 * o moved the eeprom support functions to bt_eepromlib
 * o moved the mqtt support functions to bt_mqttlib
 * o cleaned up the data compartmentalization and removed some global variables
 * o tested with the TEST_REM and GARAGE_REM
 *   (seems to be an issue with the SSR in that it stops working after a few days)
 * + added debug code to do some system monitoring, including free heap (memory leaks)
 * + started adding support for MCP23017
 * + added a re-subscribe to the case where mqtt connection is lost/regained
 * 
 * v1.6
 * + Seems that Adafruits newest ADS1015 library (v2.2.1) was changed to require #include <Adafruit_ADS1X15.h>
 * + Added debug_level to eeprom ... still in the process of implementing functionality
 * + added check on entered string length in getone_eeprom_input() to prevent overflow
 * o Checked that it builds under each hardware configuration with the new libs and IDE versions
 * + Implemented buffer length checks on EEPROM input
 * 
 * v1.5
 * + declaring v1.4 completeARDUINO_ESP8266_WEMOS_D1MINI
 * + added int parm_to_value(char *topic, bool *num_value) for SSR topic
 * + added #define GARAGE_REM for the new hardware configuration
 * + added handling for input of a DOOR status
 * + added management of gpio pins via #define's and data structure
 * + added SSR control via subscribe to mqtt topic (typically set by nodered)
 * + added BME680 support for temp, hum, pressure, VOC
 * + added topic and sending of WIFI RSSI
 * NOTE: not yet tested for the ENV_REM and CNC_REM configurations
 *       (compiles ok at this writing)
 * 
 * v1.4 (in mostly chronological order):
 * + solved the issue with local/node-red neopixel mode sync (chg in nodered flow)
 * + implemented a momentary contact switch input on MOM_SWITCH
 *   (initially on pin 16, with no end of issues ... moved to pin 14)
 *   (also, implemented as interrupt driven input)
 * + implemented manual change of neopixel mode with momentary contact switch
 * + implemented structure to make adding more EEPROM parameters easier
 * + added sample time offset and calibration parameters to EEPROM
 * + added separate calibrations to thermistor structure
 * + added more parameters to the EEPROM based data(E.g. timezone, cal factors.)
 * + fixed THERMISTORS to indicate their presence and had to add THERM_CNT
 * + added back MICS5524 gas calculation
 * + added #define for ESTOP, mostly to keep it from being sent via mqtt
 * + Configured back to the original surveillance hardware (#define config)
 *   (tested gas, temp, humidity, stime ... looks good on the bench)
 * 
 * v1.3 (in chronological order):
 * + add/rename the topics to accomodate the second, cnc router based data module
 * + clarified/added pin defines and added direction setup
 * + added a reboot on repeated failures of the wifi connect
 *   (analyzed the logs on the pi side and didn't see any indication that the
 *    broker is crashing.  Let's try the retry on this side.)
 * + adjusted the WIFI connect behavior.  In the beginning this code was for acquitision only.
 *   So, it didn't make sense to continue if WIFI wasn't connected (i.e. wait forever for connect).
 *   However, now there are some local functions, so continue, but retry the connect.
 * + Added optional device reset on repeated mqtt connect failures
 * + Added conversion of the INA169 adc channel to amps and publish
 * + Added neopixel display of current as color
 * + Added mqtt subscribe capability and topic for neopixel mode
 * + Added a super simple json parser to handle the neopixel mode
 * + Added several neopixel modes that are selected via the mqtt topic
 *   NOTE: at this writing there is precious little error handling, etc.
 *         and this implementation is a bit brute-force.
 *         use with caution until the error handling version is posted
 * + Added some better error checking for json, etc.
 * + Added ability to handle incoming messages from multiple topics
 * + Added handling of the neopxl_range parameter: 
 *   like a gain on the mapping from current to neopixel color
 * + FIXED A MAJOR BUG IN json parser relating to termination of strings
 * + made some things more efficient and reduced the fast cycle time to 50mS
 * + implemented _INIT topics to sync nodered w/ the neopxl_mode and _range
 *   (worked for _range, but need to work out on nodered how to use _mode)
 * + added some basic parameters to the EEPROM and implemented a way to interrupt
 *   the boot to enter new data and store in the EEPROM
 * + added support for ACS758 Hall Effect current sensor
 *   NOTE: I didn't have a chance yet to go back and test the INA169 path
 *
 * 
 * v1.2:
 * + created the data structure for the running average and implemented it
 * + MQTT send for a/d is running average
 * + moved the HTU21D t/h read to the slow loop for this application
 *   (It takes about 80mS to read it and I wanted the a/d acqusition to run 10X/sec)
 * 
 * v1.1:
 * + Created this new major version to consolidate functionality including neoPixel functionality
 * + Worked out the basic formulas for converting the thermistor input to physical units
 * + added thermistor values to the mqtt/json packet
 * + replaced a standard delay() at the end of loop() with a timer
 * + added a faster acquisition timer/loop
 * 
 * v0.9:
 * +adjusted WIFI LED pin number and sense in several places
 * +adjusted the mosquitto parms for playing around
 * +added some test code for raw a/d single ended readings
 * 
 * v0.8:
 * + created json object with location, timestamp in addition to just the value
 *   changed all publishes to use the new object
 * 
 * v0.6:
 * + added #define's for hardware presence (e.g. HTU21D present)
 * + added math to convert from voltage to RS to PPM (use at your own risk)
 *   ( some of the transfer function doesn't make sense to me.)
 * 
 * v0.5:
 * + changed all of the topics/etc to zk
 * 
 * v0.4:
 *  + added ntp client code and send sample time along with the data
 *  + added with WiFi connect retry in the main loop if the connection is lost
 *  + added temp and hum offsets
 * 
 * v0.3:
 * + Built and running on the target board with ESP-12S (AF P2491)
 * + add the differential read and unit conversion for gas sensor
 * + add gas sensor and offset values
 * 
 * v0.2:
 * + added a function to for the wifi connection sequence
 * + add an LED to indicate WiFi connected and check the connection
 *   each time through the loop and set the LED accordingly
 * + implement some retry on mqtt publish and wifi connections
 *   (mqtt seems to drop connection after a couple of days)
 *   
 * v0.1:
 * + remove sending the test message from the loop()
 * + convert the message format to json
 * + add the ADC acquisition and sending code - test with pot
 ******************************************************************************************************
 */

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <Adafruit_HTU21DF.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <user_interface.h>  /* for os timer functionality */
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "Adafruit_BME680.h"
#include <bt_mqttlib.h>
#include <bt_eepromlib.h>

#ifdef ARDUINO_ESP8266_WEMOS_D1MINI
#include "display_parameters.h"
#endif

/*
 * ************** PICK WHICH SENSING HARDWARE ***************************
 * 
 * pick for which hardware this software is being built
 * 
 * (CHOOSE ONLY ONE OPTION by putting "//" before the other one)
 * 
 * This also determines: 
 *  -> the set of MQTT topics to send
 *  -> the gpio pin use and direction
 *  -> which i2c-based modules are present
 *  
 *  NOTE: there are conflicts between the configurations.
 *  
 *  NOTE: many of the variables associated with the various devices are
 *        not conditionally declared (i.e. declared whether the device is present
 *        or not.  You could save some memory space by hiding them behind
 *        the #defines.
 * 
 */
//#define CNC_REM
#define ENV_REM
//#define GARAGE_REM
//#define TEST_REM

/********************* HARDWARE Connections ********************/
/*
 *
 * Input pull-ups reported to be 30-100K
 * NOTE: these #defines are for referencing pins.  You must insure that
 * the way you intend to use the referenced pins in your code matches the
 * initialization structure below.
 * 
 * NOTES: 1) There is conflicting information about which pins have which pullups.
 *           Some sources say that Pin 16 has a build in pull-down.
 *           Pin 15 behaves as if it has a built-in pull-down.  The neopixels don't seem
 *           to mind.  Actually, Pin 16 just behaves strangely in this application: avoid it
 *        2) With, at least, Pin 16, INPUT_PULLUP does not seem to enable a pull-up
 *           i.e. the input seems to float.  If an external 10K pull-up is added,
 *           it behaves as expected.  Same behavior with INPUT alone. (without the level shifter)
 *        3) Conclusion, generally manage your own pull-ups externally.
 *        4) I don't know what's going on with Pin 16.
 */


/*
 * define what is to be done for initialization of the pins
 * 
 * NOTE: you have to manage how these pins are accessed.
 * i.e. there is no error checking if you code a read on a write pin.
 * You also have to make sure that there are no conflicts between 
 * what's defined here and the hardware modules that are included above.
 */
struct pin_init {
  int  pin;      /* pin number */
  char *label;  /* human readable meaningful label for the pin */
  int  pin_mode; /*INPUT, OUTPUT, or INPUT_PULLUP */
  bool action;  /* true: do initialization; false: do nothing for this pin */
};


#ifdef TEST_REM

//#define UNUSED      0  /* built in LED on Huzzah */
#define   ACQ_ACTIVE  2  /* sampling loop for scope monitoring (also blue on-board LED) */
//#define I2C_SDA     4  /* defined elsewhere */
//#define I2C_SCL     5  /* defined elsewhere */
#define   WIFI_LED    12 /* onboard LED to indicate WIFI connection */
#define   GDOOR_PIN   13  /* input to provide door open/closed microswitch state */
#define   SSR_PIN     14  /* pin for locally connected Solid State Relay */

struct pin_init local_pins[] = {
  {0,  "BD_LED",   OUTPUT,       false},  /* also tied to FLASH push button */
  {2,  "SAMPLE",   OUTPUT,       true},   /* also the blue onboard led and pulled up*/
  {4,  "I2C_SDA",  OUTPUT,       false},  /* initialized by the i2c class */
  {5,  "I2C_SCL",  OUTPUT,       false},  /* initialized by the i2c class */
  {12, "WIFI LED", OUTPUT,       true},   /* used by SPI if configured */
  {13, "GRGDOOR",  INPUT,        true},   /* used by SPI if configured */
  {14, "SSR",      OUTPUT,       true},   /* used by SPI if configured */
  {15, "UNUSED",   OUTPUT,       false},  /* used by SPI if configured ... and pulled down */
  {16, "UNUSED",   OUTPUT,       false},  /* high at boot deep sleep wakeup */
  {-1, "end",      INPUT,        false},  /* terminate the list */
};

struct pin_init mcp_pins[] = {
  {0,  "UNUSED",   OUTPUT,       false},
  {1,  "UNUSED",   OUTPUT,       false},
  {2,  "UNUSED",   OUTPUT,       false},
  {3,  "UNUSED",   OUTPUT,       false},
  {4,  "UNUSED",   OUTPUT,       false},
  {5,  "UNUSED",   OUTPUT,       false},
  {6,  "UNUSED",   OUTPUT,       false},
  {7,  "UNUSED",   OUTPUT,       false},
  {8,  "UNUSED",   OUTPUT,       false},
  {9,  "UNUSED",   OUTPUT,       false},
  {10, "UNUSED",   OUTPUT,       false},
  {11, "UNUSED",   OUTPUT,       false},
  {12, "UNUSED",   OUTPUT,       false},
  {13, "UNUSED",   OUTPUT,       false},
  {14, "UNUSED",   OUTPUT,       false},
  {15, "UNUSED",   OUTPUT,       false},
};

#endif

#ifdef CNC_REM

//#define UNUSED       0  /* built in LED on Huzzah */
#define   ACQ_ACTIVE   2  /* sampling loop for scope monitoring (also blue on-board LED) */
//#define I2C_SDA      4  /* defined elsewhere */
//#define I2C_SCL      5  /* defined elsewhere */
#define   WIFI_LED    12 /* oops ... see below ... led connected to diff pins */
#define   ESTOP_PIN   13  /* input to provide the status of the router estop */
#define   MOM_SWITCH  14  /* momentary switch input for various uses */
#define   NEOPXL_PIN  15  /* output pin for neopixel data (built-in 30-100K pull-down)*/

struct pin_init local_pins[] = {
  {0,  "BD_LED",   OUTPUT, false},
  {2,  "SAMPLE",   OUTPUT, true},   /* also the blue onboard led */
  {4,  "I2C_SDA",  OUTPUT, false},  /* initialized by the i2c class */
  {5,  "I2C_SCL",  OUTPUT, false},  /* initialized by the i2c class */
  {12, "WIFI LED", OUTPUT, true},
  {13, "ESTOP",    INPUT,  true},
  {14, "MOM SW",   INPUT,  true},
  {15, "NEOPXL",   OUTPUT, true},
  {16, "UNUSED",   OUTPUT, false},
  {-1, "end",      INPUT,  false},  /* terminate the list */
};
#endif


#ifdef ENV_REM

//#define UNUSED      0  /* built in LED on Huzzah */
#define   ACQ_ACTIVE  2  /* sampling loop for scope monitoring (also blue on-board LED) */
//#define I2C_SDA     4  /* defined elsewhere */
//#define I2C_SCL     5  /* defined elsewhere */
#define   WIFI_LED   13 /* oops ... see below ... led connected to diff pins */

struct pin_init local_pins[] = {
  {0,  "BD_LED",   OUTPUT,  false},
  {2,  "SAMPLE",   OUTPUT, true},   /* also the blue onboard led */
  {4,  "I2C_SDA",  OUTPUT, false},  /* initialized by the i2c class */
  {5,  "I2C_SCL",  OUTPUT, false},  /* initialized by the i2c class */
  {12, "UNUSED",   OUTPUT, false},
  {13, "WIFI LED", OUTPUT, true},
  {14, "UNUSED",   OUTPUT, false},
  {15, "UNUSED",   OUTPUT, false},
  {16, "UNUSED",   OUTPUT, false},
  {-1, "end",      INPUT,  false},  /* terminate the list */
};

#endif


#ifdef GARAGE_REM

//#define UNUSED      0  /* built in LED on Huzzah */
#define   ACQ_ACTIVE  2  /* sampling loop for scope monitoring (also blue on-board LED) */
//#define I2C_SDA     4  /* defined elsewhere */
//#define I2C_SCL     5  /* defined elsewhere */
#define   WIFI_LED    12 /* onboard LED to indicate WIFI connection */
#define   GDOOR_PIN   13  /* input to provide door open/closed microswitch state */
#define   SSR_PIN     14  /* pin for locally connected Solid State Relay */

struct pin_init local_pins[] = {
  {0,  "BD_LED",   OUTPUT,       false},  /* also tied to FLASH push button */
  {2,  "SAMPLE",   OUTPUT,       true},   /* also the blue onboard led and pulled up*/
  {4,  "I2C_SDA",  OUTPUT,       false},  /* initialized by the i2c class */
  {5,  "I2C_SCL",  OUTPUT,       false},  /* initialized by the i2c class */
  {12, "WIFI LED", OUTPUT,       true},   /* used by SPI if configured */
  {13, "GRGDOOR",  INPUT,        true},   /* used by SPI if configured */
  {14, "SSR",      OUTPUT,       true},   /* used by SPI if configured */
  {15, "UNUSED",   OUTPUT,       false},  /* used by SPI if configured ... and pulled down */
  {16, "UNUSED",   OUTPUT,       false},  /* high at boot deep sleep wakeup */
  {-1, "end",      INPUT,        false},  /* terminate the list */
};

#endif

/*
 * initialize the gpio pins according to the structure
 * defined above
 */
void init_pins()  {
  int i = 0;
  while(local_pins[i].pin >= 0)  {
    if(local_pins[i].action == true)
      pinMode(local_pins[i].pin, local_pins[i].pin_mode);
    i++;
  }
}

/* 
 *  which hardware is actually connected?
 *  (divided into the configurations (e.g. ENV_REM and CNC_REM) ... selected above)
 *  
 *  note: you have to manage dependencies yourself
 *        (e.g. if no adc, thermistors and current not possible)
 *  I used 1's and 0's since I needed some #if's in the code below.
 *  comment out the line to mark as NOT present (i.e. #undef).
 *  
 *  NOTE: you have to comment out the hardware that isn't there
 *  to allow multiple sensor devices to exist using the same
 *  data broker.  i.e. don't want two sensors sending the same topic,
 *  especially if the hardware to generate the data is not present.
 *  
 */

#ifdef TEST_REM

#define WIFI_ON          1  // Should wifi be enabled; used for debug, not fully vetted
//#define HTU21DF_P        1  // Is the temp/hum module present
//#define ADS1015_P      1  // Is the A/D present
//#define MICS5524_P     1  // Is the gas sensor present (requires ADS1015_P)
//#define THERMISTORS    1  // Are we using the A/D to sense thermistors
//#define INA169         0  // Is the current shunt present (choose one: INA169 or ACS759)
//#define ACS758         1  // Hall Effect current sensor present
//#define NEOPIXELS      1  // neopixels are present
//#define ESTOP          1  // is there an estop status wire connected
//#define MOM_SWITCH_P   1
//#define LOCAL_SSR      1  // solid state relay control
//#define GDOOR_SENSE    1  // garage door sense switch
//#define BME680         1  // temp, hum, pressure, voc
//#define MCP23017       1  // additional 16 gpio pins on separate chip
#define GFX_DISP         1  // graphics touchscreen
//#define SEND_TSTAMP      1  // pseudo hardware to control whether to send timestamp
//#define WIFI_LED_CON     LOW // the value of the WIFI_LED when successfully connected
//#define WIFI_LED_DIS     HIGH  // value of the WIFI_LED when successfully *not* connected


#endif

#ifdef ENV_REM

#define WIFI_ON          1  // Should wifi be enabled; used for debug, not fully vetted
#define HTU21DF_P        1  // Is the temp/hum module present
#define ADS1015_P        1  // Is the A/D present
#define MICS5524_P       1  // Is the gas sensor present (requires ADS1015_P)
//#define THERMISTORS    1  // Are we using the A/D to sense thermistors
//#define INA169         0  // Is the current shunt present (choose one: INA169 or ACS759)
//#define ACS758         1  // Hall Effect current sensor present
//#define NEOPIXELS      1  // neopixels are present
//#define ESTOP          1  // is there an estop status wire connected
//#define MOM_SWITCH_P   1
//#define MCP23017       1  // additional 16 gpio pins on separate chip
#define SEND_TSTAMP      1  // pseudo hardware to control whether to send timestamp
#define WIFI_LED_CON     HIGH // value of the WIFI_LED when successfully connected
#define WIFI_LED_DIS     LOW  // value of the WIFI_LED when successfully *not* connected

#endif


#ifdef CNC_REM

#define WIFI_ON          1  // Should wifi be enabled; used for debug, not fully vetted
//#define HTU21DF_P      1  // Is the temp/hum module present
#define ADS1015_P        1  // Is the A/D present
//#define MICS5524_P     1  // Is the gas sensor present (requires ADS1015_P)
#define THERMISTORS      1  // Are we using the A/D to sense thermistors
//#define INA169         0  // Is the current shunt present (choose one: INA169 or ACS759)
#define ACS758           1  // Hall Effect current sensor present
#define NEOPIXELS        1  // neopixels are present
#define ESTOP            1  // is there an estop status wire connected
#define MOM_SWITCH_P     1
//#define MCP23017       1  // additional 16 gpio pins on separate chip
#define SEND_TSTAMP      1  // pseudo hardware to control whether to send timestamp
#define WIFI_LED_CON     LOW // the value of the WIFI_LED when successfully connected
#define WIFI_LED_DIS     HIGH  // value of the WIFI_LED when successfully *not* connected

#endif


#ifdef GARAGE_REM

#define WIFI_ON          1  // Should wifi be enabled; used for debug, not fully vetted
//#define HTU21DF_P      1  // Is the temp/hum module present
//#define ADS1015_P      1  // Is the A/D present
//#define MICS5524_P     1  // Is the gas sensor present (requires ADS1015_P)
//#define THERMISTORS    1  // Are we using the A/D to sense thermistors
//#define INA169         0  // Is the current shunt present (choose one: INA169 or ACS759)
//#define ACS758         1  // Hall Effect current sensor present
//#define NEOPIXELS      1  // neopixels are present
//#define ESTOP          1  // is there an estop status wire connected
//#define MOM_SWITCH_P   1
#define LOCAL_SSR        1  // solid state relay control
#define GDOOR_SENSE      1  // garage door sense switch
#define BME680           1  // temp, hum, pressure, voc
//#define MCP23017       1  // additional 16 gpio pins on separate chip
#define SEND_TSTAMP      1  // pseudo hardware to control whether to send timestamp
#define WIFI_LED_CON     LOW // the value of the WIFI_LED when successfully connected
#define WIFI_LED_DIS     HIGH  // value of the WIFI_LED when successfully *not* connected

#endif

/********************* MQTT Payload Information ***************/

//Publish:

#ifdef TEST_REM
// This section is used for the zhome garage sensing module
#define TOPIC_WIFI_RSSI  "bt-teststand/wifi_rssi"
#define TOPIC_ENV_TEMP   "bt-garage/temp"
#define TOPIC_ENV_HUM    "bt-garage/humidity"
#define TOPIC_ENV_PRES   "bt-garage/pressure"
#define TOPIC_ENV_ALT    "bt-garage/altitude"
#define TOPIC_ENV_GASR   "bt-garage/gasohms"
#define TOPIC_ENV_GDOOR  "bt-garage/grgdoor"
#define TOPIC_STIME      "bt-garage/time"

// This section is used for the free air sensing module
//#define TOPIC_WIFI_RSSI  "zk-env/wifi_rssi"
//#define TOPIC_ENV_TEMP   "zk-env/temp"
//#define TOPIC_ENV_HUM    "zk-env/humidity"
//#define TOPIC_STIME      "zk-env/time"
//#define TOPIC_ENV_GASCO  "zk-env/gasco"
//#define TOPIC_ENV_GASPR  "zk-env/gaspr"
//#define TOPIC_ENV_GASRW  "zk-env/gasrw"


// This section is used for the CNC Router sensing module
//#define TOPIC_ENV_TEMP    "zk-cncrtr/temp"
//#define TOPIC_ENV_HUM     "zk-cncrtr/humidity"
//#define TOPIC_STIME       "zk-cncrtr/time"
#define TOPIC_ENV_THERM0  "zk-cncrtr/therm0"
#define TOPIC_ENV_THERM1  "zk-cncrtr/therm1"
#define TOPIC_ENV_AAMPS   "zk-cncrtr/aamps"
#define TOPIC_ENV_ESTOP   "zk-cncrtr/estop"

#endif


#ifdef ENV_REM

// This section is used for the free air sensing module
#define TOPIC_WIFI_RSSI  "zk-env/wifi_rssi"
#define TOPIC_ENV_TEMP   "zk-env/temp"
#define TOPIC_ENV_HUM    "zk-env/humidity"
#define TOPIC_STIME      "zk-env/time"
#define TOPIC_ENV_GASCO  "zk-env/gasco"
#define TOPIC_ENV_GASPR  "zk-env/gaspr"
#define TOPIC_ENV_GASRW  "zk-env/gasrw"


// This section is used for the CNC Router sensing module
//#define TOPIC_ENV_TEMP    "zk-cncrtr/temp"
//#define TOPIC_ENV_HUM     "zk-cncrtr/humidity"
//#define TOPIC_STIME       "zk-cncrtr/time"
#define TOPIC_ENV_THERM0  "zk-cncrtr/therm0"
#define TOPIC_ENV_THERM1  "zk-cncrtr/therm1"
#define TOPIC_ENV_AAMPS   "zk-cncrtr/aamps"
#define TOPIC_ENV_ESTOP   "zk-cncrtr/estop"

#endif

#ifdef CNC_REM

// This section is used for the free air sensing module
//#define TOPIC_WIFI_RSSI  "zk-env/wifi_rssi"
//#define TOPIC_ENV_TEMP   "zk-env/temp"
//#define TOPIC_ENV_HUM    "zk-env/humidity"
//#define TOPIC_STIME      "zk-env/time"
#define TOPIC_ENV_GASCO  "zk-env/gasco"
#define TOPIC_ENV_GASPR  "zk-env/gaspr"
#define TOPIC_ENV_GASRW  "zk-env/gasrw"


// This section is used for the CNC Router sensing module
#define TOPIC_WIFI_RSSI   "zk-cncrtr/wifi_rssi"
#define TOPIC_ENV_TEMP    "zk-cncrtr/temp"
#define TOPIC_ENV_HUM     "zk-cncrtr/humidity"
#define TOPIC_STIME       "zk-cncrtr/time"
#define TOPIC_ENV_THERM0  "zk-cncrtr/therm0"
#define TOPIC_ENV_THERM1  "zk-cncrtr/therm1"
#define TOPIC_ENV_AAMPS   "zk-cncrtr/aamps"
#define TOPIC_ENV_ESTOP   "zk-cncrtr/estop"

#endif


#ifdef GARAGE_REM

// This section is used for the zhome garage sensing module
#define TOPIC_WIFI_RSSI  "bt-garage/wifi_rssi"
#define TOPIC_ENV_TEMP   "bt-garage/temp"
#define TOPIC_ENV_HUM    "bt-garage/humidity"
#define TOPIC_ENV_PRES   "bt-garage/pressure"
#define TOPIC_ENV_ALT    "bt-garage/altitude"
#define TOPIC_ENV_GASR   "bt-garage/gasohms"
#define TOPIC_ENV_GDOOR  "bt-garage/grgdoor"
#define TOPIC_STIME      "bt-garage/time"

#endif

//Subscribe:

/*
 * retain the status of the subscription request.
 * used in retry logic.
 */
bool mqtt_subscribed = false;

/* 
 * Potential list of topics to which to Subscribe: 
 * (see below structure initialization for those which are used)
 */
#define TOPIC_NEOPXL_MODE  "zk-cncrtr/neopxl_mode"
#define TOPIC_NEOPXL_RANGE "zk-cncrtr/neopxl_range"
#define TOPIC_SSR_STATE    "bt-garage/ssr_state"

// Special initialization topics
// (sent once at startup to sync nodered w/ this software)
#define TOPIC_NEOPXL_MODE_INIT  "zk-cncrtr/neopxl_mode_init"
#define TOPIC_NEOPXL_RANGE_INIT "zk-cncrtr/neopxl_range_init"


#ifdef CNC_REM
struct parameter parameters[] = {
  {TOPIC_NEOPXL_MODE,  "", "", PARM_INT, false, true},
  {TOPIC_NEOPXL_RANGE, "", "", PARM_INT, false, false},
  {"","","",PARM_UND, false},  /* terminate the list */
};
#endif

#ifdef TEST_REM

struct parameter parameters[] = {
  {TOPIC_ENV_TEMP,  "Garage Temp",     "", PARM_FLOAT,  false, true},
  {TOPIC_ENV_HUM,   "Garage Humidity", "", PARM_FLOAT,  false, true},
  {TOPIC_ENV_PRES,  "Garage Pressure", "", PARM_FLOAT,  false, true},
  {TOPIC_ENV_ALT,   "Garage Altitude", "", PARM_FLOAT,  false, true},
  {TOPIC_ENV_GASR,  "Garage GasOhms",  "", PARM_FLOAT,  false, true},
  {TOPIC_ENV_GDOOR, "Garage Door",     "", PARM_INT,    false, true},
  {TOPIC_STIME,     "Garage Sample t", "", PARM_STRING, false, true},
  {"","","",PARM_UND, false, false},  /* terminate the list */
};
#endif

#ifdef ENV_REM
struct parameter parameters[] = {
  {"","","",PARM_UND, false, false},  /* terminate the list */
};
#endif

#ifdef GARAGE_REM
struct parameter parameters[] = {
  {TOPIC_SSR_STATE,  "", "", PARM_INT, false, true},
  {"","","",PARM_UND, false, false},  /* terminate the list */
};
#endif

/*
 * DEBUG functions
 * ---------------
 * initialize the local debug message level with
 * the one set, probably from EEPROM
 */
int l_debug_level = 0;

void l_debug_set(int level)  {
  l_debug_level = level;
}

/*
 * display  debug message without a newline
 * if the level is at or above the initialized level
 */
void l_debug(char *msg, int level)  {
  if(level <= l_debug_level)
    Serial.print(msg);
}
void l_debug(int msg, int level)  {
  if(level <= l_debug_level)
    Serial.print(msg);
}
void l_debug(float msg, int level)  {
  if(level <= l_debug_level)
    Serial.print(msg);
}

/*
 * display  debug message with a newline
 * if the level is at or above the initialized level
 */
void l_debugln(char *msg, int level)  {
  if(level <= l_debug_level)
    Serial.println(msg);
}
void l_debugln(int msg, int level)  {
  if(level <= l_debug_level)
    Serial.println(msg);
}
void l_debugln(float msg, int level)  {
  if(level <= l_debug_level)
    Serial.println(msg);
}



/********************* BEHAVIORAL Characteristics *************/

/*
 * timer intervals for the main sensing loop and publish
 */
#define MQTT_INTERVAL     2000 // mS between publish's
#define ACQ_INTERVAL      50   // A/D sampling interval (mS)

#define RST_ON_WIFI_FAIL  false // If true, reset the device after RST_ON_WIFI_COUNT loops
#define RST_ON_WIFI_COUNT 30    // Number of times through the main loop sample/publish timer
#define INITIAL_WIFI_WAIT 10    // How long to wait for the initial connect (n 500 mS loops)
#define RST_ON_MQTT_FAIL  true  // If true, reset the device after RST_ON_MQTT_COUNT loops
#define RST_ON_MQTT_COUNT 10

/*
 * To count down the WiFi and/or MQTT failures
 */
int wifi_fails = RST_ON_WIFI_COUNT;
int mqtt_fails = RST_ON_MQTT_COUNT;

/*
 * display debug messages in the loop if defined
 * the messages really don't take much time, however ... maybe 10 mS
 */
#define L_DEBUG_MSG     // for the loop driven by MQTT_INTERVAL
//#define FL_DEBUG_MSG  // beware, if ACQ_INTERVAL is small, this will pump out a lot of messages
//#define FFL_DEBUG_MSG // some really detailed stuff

/*
 * set up the structure and callback() for the slower loop functionality
 * add the volatile qualifier to insure RAM location and safe access
 */
os_timer_t sampleTimer; /* for the slower sample loop */
volatile bool sampleTimerOccured;
void sampleTimerCallback(void *pArg)  {
  sampleTimerOccured = true;
}

/*
 * faster loop
 */
os_timer_t acqTimer;
volatile bool acqTimerOccured;
void acqTimerCallback(void *pArg)  {
  acqTimerOccured = true;
}
 
/*
 * for calculating loop timing during development
 */
unsigned long currentMillis = 0, previousMillis = 0;


/*
 * string to match for validation
 * this indicates the version/structure of the EEPROM too.
 * be sure to update this string if you change the 
 * net_config struct below.
 */
#define EEPROM_VALID  "valid_v1.6.1"

/*
 * pointer for direct access to mon_config structure
 * (see bt_eepromlib)
 */
struct net_config *pmon_config;

/* 
 * ADS1015 12-bit (i2c) ADC
 * ------------------------
 * 
 * ADC gain
 * 
 * With GAIN_TWO full scale is +/- 2.048 v according to the data sheet.
 * 12 bit adc(i.e. +/- 11-bits (2048)) results in a perfect 1 mV/bit.
 * 
 * Acquisition behavior:
 * A timer based on ACQ_INTERVAL (above) triggers acqusition of the a/d channels.
 * Multiple samples-per-sample can be specified by ADC_SAMPLES and
 * ADC_INTERVAL.  This is meant to run fast and help with electrical noise,
 * for example.
 * 
 * A running average is produced from the last ADC_AVG SAMPLES in a rotating buffer array
 * per channel, adc[][].  Each ADC_AVG_SAMPLES an average is written to adc_ravg[].
 * 
 * On the -> CNC Monitoring PCB <-, the following ADC channels are allocated:
 * (All are single ended)
 * ADC_0 : ACS758 *or* INA169 Current Measuring
 * ADC_1 : Thermistor #1  (see struct thermistor therms[THERM_CNT] below)
 * ADC_2 : unallocated
 * ADC_3 : Thermistor #2  (see struct thermistor therms[THERM_CNT] below)
 * 
 * On the ORIGINAL -> shop environmental remote device <-, the following channels are allocated:
 * NOTE: 1) these channel allocations are  NOT compatible with the
 *          hardwired connections of the CNC Monitoring PCB
 *       2) The gain setting GAIN_TWO is compatible with both hardware sets
 * ADC_0 : MICS5524     \
 *         (adjustable)  -- differencial read for gas sensor (ads.readADC_Differential_0_1() )
 * ADC_1 : Bias Voltage /   (allowing the hardware to take the difference allocates the
 * ADC_2 : unallocated       full range of the A/D to the difference)
 * ADC_3 : unallocated
 * 
 * Porting this code to the shop environmental remote device
 * (i.e. run same software, with improvements, on CNC Monitor and shop env rem device)
 * 
 * I noticed in implementing the gas sensor that the bias voltage (adjustable
 * using an on-board pot) actually was close to 0 volts.  So, the differential reading
 * was probably a bit overkill (did it because I could sort of thing).  So I'm going
 * to do the subtraction in software, still leaving almost all of the A/D dynamic 
 * range for the gas sensor reading:
 * ADC_0 : MICS5524     \
 *                       -- raw gas reading will be just based on adc_ravg[1] - adc_ravg[0]
 * ADC_1 : Bias Voltage /   (i.e. the running average since it changes relatively slowly)
 * ADC_2 : unallocated
 * ADC_3 : unallocated
 * 
 */
#define ADC_GAIN         GAIN_TWO /* +/- 2.048 V  */
#define ADC_CHANNELS     4  /* number of channels in the ADC */

#define ADC_SAMPLES      1  /* just for electrical noise filtering */
#define ADC_INTERVAL     1  /* mS between averaged samples */
double  adcsum = 0;         /* accumulator used for averaging */

#define ADC_AVG_SAMPLES  10  /* samples to be averaged for running average */

int i, j, k;  /* looping parameters; not intended to be persistent */

int16_t adc[ADC_CHANNELS][ADC_AVG_SAMPLES]; /* raw adc readings */
int16_t adc_ravg[ADC_CHANNELS];  /* running average of adc channels */

#ifdef ADS1015_P
// Instantiate the ADC for the gas sensor
Adafruit_ADS1015 ads;
#endif

/*
 * return the volts per bit for the adc
 * (note: may not be used in all calculations)
 */
float adc_V_per_bit(adsGain_t adc_gain)  {
  float vpb = -1;
  
  switch(adc_gain) {
    case GAIN_TWOTHIRDS:
      vpb = 0.003;
      break;
    case GAIN_ONE:
      vpb = 0.002;
      break;
    case GAIN_TWO:
      vpb = 0.001;
      break;
    case GAIN_FOUR:
      vpb = 0.0005;
      break;
    case GAIN_EIGHT:
      vpb = 0.00025;
      break;
    case GAIN_SIXTEEN:
      vpb = 0.000125;
      break;
    default:
      break;
  }
  return(vpb);
}

/*
 * THERMISTOR
 * ----------
 * used to convert thermistor adc values to physical units
 * 
 *   VS = 3.3V
 *    |
 *    /
 *    \  seriesR
 *    /
 *    |
 *    --- ADC input -> physically connected to adc[1] and adc [3]
 *    |
 *    /
 *    \  thermR
 *    /
 *    |
 *   GND
 *   
 *   To get to the calculated thermistor resistance:
 *    therms[i].thermR = seriesR / ((adcMax/adc_ravg[therms[i].adc_ch]) - 1);
 *    
 *   To get from the thermistor resistance to the temp in degrees Kelvin:
 *    therms[i].tempK = 1.00 / (invT0 + invBeta * (log(therms[i].thermR/T0R)));
 *   
 *   Calibration coefficients:
 *     T0_Resistance = Resistance value at calibration temp, T0
 *     Beta = from the manufacturer for the type of thermistor used
 */
#define THERM_CNT 2  /* number of thermistors present */
const float adcMax = 3336.00;  /* more precise 3.3v, measured */
const float invBeta = 1.00 / 3950.00;  /* thermistor beta parameter from the manufacturer */
const float invT0 = 1.00 / (22.06 + 273.15);  /* 1/T0 in deg K, inverse of thermistor calibration point */
const float T0R = 107592.00;  /* thermistor resistance at T0  */
const float seriesR = 100000.00;  /* series resistor */

struct thermistor  {
  int adc_ch;
  float calBeta;
  float invBeta;
  float calT0;
  float invT0;
  float calT0R;
  float tempK;
  float tempC;
  float tempF;
  float thermR;
};


struct thermistor therms[THERM_CNT] =
{
  /*
   *                                calculated values
   *                                ------------------
   *  adc_ch   Beta   invBeta  T0(degC) invT0     T0R    degK degC degF  R  
   *  ------  ------  -------   ------  -----   -------  ---  ---  ---  ---
   */
      { 1,    3950.0,   0.0,    22.06,   0.0,  107592.0, 0.0, 0.0, 0.0, 0.0},
      { 3,    3950.0,   0.0,    22.06,   0.0,  107592.0, 0.0, 0.0, 0.0, 0.0},
};

/*
 * use a little storage, and precalculate some values
 * to same time in the fast loop
 */
void init_therms(struct thermistor therms[])  {
  for(int i = 0; i< THERM_CNT; i++)  {
    therms[i].invBeta = 1.0 / therms[i].calBeta;
    therms[i].invT0 = 1.0 / (therms[i].calT0 + 273.15);
  }
}

/*
 * HIGH SIDE CURRENT MONITOR (INA169 based)
 * HALL EFFECT CURRENT SENSOR (ACS758)
 * 
 * Shunt Output Voltage (input to INA169) = Current (Amps) * INA169_shunt_R
 * E.g. INA169_shunt_R = 0.001, 1 A -> 1 mA
 * 
 * INA_169 Output Voltage = Current * INA169_shunt_R * (INA169_load_R/1000)
 * Current (Amps) = ADC 
 * 
 * Voltage Gain   load_R
 * ------------   ------
 *       1          1K
 *       2          2K
 *      ...        ...
 *      50         50K
 *      68         68K   (used in this implementation)
 *     100        100K
 * 
 * Range for this application:
 * Combined motor current   Shunt Voltage   ADC Voltage In
 * ----------------------   -------------   --------------
 *         <0                    <0                0 (INA169 prevents <0 V)
 *          0                     0                0
 *        1 Amp                 1 mV             68 mV
 *       20 Amps               20 mV             1.36V
 * (designed to be on par with thermistor voltages ... i.e. compatible ADC gain)
 */
#define     INA169_ADCCH     0  /* adc channel for ina169/shunt input */
const float INA169_shunt_R = 0.001;  /* Current measuring shund resistor */
const float INA169_load_R  = 68000;  /* Output load resistance */
const float INA169_Vgain   = INA169_load_R / (float)1000;

/*
 * these are used for whichever current conversion equation
 * is used
 */
float Aamps;  /* running average of current */
float Iamps;  /* instantaneous current value */

/*
 * Convert the input voltage to current in amps for the INA169 Current monitor
 *                 adc voltage
 *        -----------------------------------
 *                           actual shunt voltage
 *        -------------------------------------------------
 *                                    i = v/r
 *        -------------------------------------------------------------------
 */
float INA169_bits_to_amps(int adc_bits)  {
  return(((adc_bits * adc_V_per_bit(ADC_GAIN))/INA169_Vgain) / INA169_shunt_R);
}

#define ACS758_ADCCH 0  /* adc channel for acs758 current sensor */
#define ACS758_GAIN    (float)0.060   /* V/amp */
float   acs758_offset;  /* volts, hold the converted to float cal constant to not waste time in the loop */

/*
 * conversion equation for the ACS758
 */
float ACS758_bits_to_amps(int adc_bits)  {
  return(((adc_bits * adc_V_per_bit(ADC_GAIN))- acs758_offset)/ACS758_GAIN);
}

/*
 * set the pointer to the conversion function to the appropriate
 * one for the hardware that is connected
 */
#if INA169
float (*current_bits_to_amps)(int adc_bits) = INA169_bits_to_amps;
#define CURRENT_ADCCH INA169_ADCCH
#elif ACS758
float (*current_bits_to_amps)(int adc_bits) = ACS758_bits_to_amps;
#define CURRENT_ADCCH ACS758_ADCCH
#endif

void ACS758_set_offset()  {
  ;
}

/*
 * MiCS5524 GAS SENSOR
 * -------------------
 * convert the adc reading to PPM.
 * use the appropriate formula for different gasses.
 * remember that the sensor cannot distinguish which gas is causing
 * the reading.  so, this just calculates various PPM's as if the sensor
 * was responding to a particular gas.
 * 
 * 0 = no conversion
 * 1 = CO
 * 2 = propane
 * 
 *
 *****
 *** These formula's are a guess !!!  *** NOT CALIBRATED ***
 *** Under absolute no circumstances should this software/firmware alone or in
 *** combination with hardware on which it was designed to run be used 
 *** to make safety and/or health decisions.
 ***
 *** Do not use it to determine if a
 *** location is safe for human or animal occupancy.  It was developed solely
 *** for hobby and learning purposes.
 *****
 */
#define PASSTHRU 0
#define CARBMONO 1
#define PROPANE  2
#define VS       (float)5.19   // supply voltage
#define R0       (float)164161 // sensor resistance in air (no CO) ... measured
#define RL       (float)10000  // load resistor; fixed in hardware
#define VPBIT    (float)0.002  // V / bit; remember hardware voltage divider

float gas_v_to_ppm(int formula, float bits)  {

  float ppm = (float)-1;
  float rs;

  /*
   * avoid divide by zero and neg powers
   * (should never occur in the real world or outside
   * of testing and debug.  bits = 1 is arbitrary.)
   */
  if(bits <= 0)
    bits = 1;

  /*
   * calculate the resistance of the element
   * (the * 2 is because of the hardwired voltage divider)
   */
  rs = RL * ((VS/((adc_V_per_bit(ADC_GAIN) * 2) * bits)) - 1);
#ifdef FL_DEBUG_MSG
  Serial.print("in gas_v_to_ppm() bits = "); Serial.println(bits);
  Serial.print("MiCS5524 GAS SENSOR rs = "); Serial.println(rs);
#endif 
  
  switch(formula)  {
    case PASSTHRU:
      ppm = VPBIT * bits;
      break;

    case PROPANE:
      ppm = (float)10500 * pow(((rs/R0)/0.065),-2.2561);
      break;

    case CARBMONO:
      ppm = (float)4.7 * pow(((rs/R0)),-1.1792);
      break;

    default:
      break;
  }
  
  return ppm;
}


/*
 * BME680
 * ------
 * Temp, Humidity, Pressure, VOC sensor
 */
#ifdef BME680

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME680 bme; // I2C

#endif


/*
 *  NEOPIXELS
 *  ---------
 */
 
#define NEOPXL_MODE_MIN 0
#define NEOPXL_MODE_MAX 2 /* maximum value of defined modes */

/*
 * holds the status of the config physical switch.
 * config_sw_pressed stays "true" until someone uses it.
 * (Note: at this writing, input 16 is used for this input.
 *  It cannot be attached to an interrupt.  So it is polled.)
 *  
 *  use volatile qualifier to insure RAM location and safe access
 */
volatile bool config_sw_pressed = false;


/*
 * interrupt service routine for momentary contact switch
 * the addition of ICACHE_RAM_ATTR was to fix the
 * "ISR not in IRAM" crash at boot issue
 */
void ICACHE_RAM_ATTR mom_switch_int()  {
  config_sw_pressed = true;
}

#define NEOPXL_COUNT  30  /* total number of neopixels in the string */
/* NEOPXL_PIN defined above in hardware section */
#define NEOPXL_BRT    (float)0.5 /* default neopixel brightness */
#define NEOPXL_WAIT   2  /* time to wait after .show() in mS */

/*
 * Define the parameters that describe neopixel playout modes
 */
#define NEOPXL_MODE_OFF    0  /* Neopixels are off */

#define NEOPXL_MODE_COLOR  1  /* play the current out on range below with color palette */
#define NEOPXL_COLOR_START 0  /* starting pixel for COLOR mode */
#define NEOPXL_COLOR_END  14  /* ending pixel for COLOR mode */
#define NEOPXL_COLOR_MIN   0  /* map this to the bottom of the color palette */
#define NEOPXL_COLOR_MAX  20  /* map this to the top of the color palette */

/* see below for VU meter #defines, functions, etc. */

#ifdef NEOPIXELS
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEOPXL_COUNT, NEOPXL_PIN, NEO_GRB + NEO_KHZ800);


/*
 * saves the neopixel mode that is sent via mqtt
 * set the value equal to the default value
 */
int neopxl_mode = NEOPXL_MODE_COLOR;  /* active neopixel mode */
int nneopxl_mode = NEOPXL_MODE_COLOR;  /* "new" neopxl_mode received */
bool neopxl_mode_valid; /* has the neopxl_mode been set by incoming mqtt message */
int neopxl_color_max = NEOPXL_COLOR_MAX;  /* map the top of neopixel color range to this */


/*
 * define a spectrum of colors from green to red for use with neopixels
 * NOTE: It's intentional that white is last to alert the operator that
 * the parameter being displayed is at the top of the range (like lightening)
 */
#define NEOPXL_PAL_MAX 12  /* a value, not a count */
const uint8_t rgb_palette[13][3] = {
   /* R   G   B */
    {  0,  0,  0},  /* off */
    {  0,255,  0},  /* green */
    { 50,255,  0},
    {100,255,  0},
    {150,255,  0},
    {200,255,  0},
    {255,255,  0},  /* yellow */
    {255,200,  0},
    {255,150,  0},
    {255,100,  0},
    {255, 50,  0},
    {255,  0,  0},  /* red */
    {255,255,255}   /* white */
  };

/*
 * Set the color of the whole neopixel strip.
 * Scale the r, g, b values by brightness (0.0 -> 1.0).
 * color_index indexes into the the rgb_palette[][] table.
 * 
 * expects global variable strip for neopixel class instance
 * int color_index   : index in the color palette[][] array to write
 * float brightness  : value from 0 to 1.0; multiplied by the palette[][] values
 * int neopxl_start  : first neopixel to which to write
 * int neopxl_end    : last neopixel to which to write
 * 
 */
void neopxl_color_palette_set(int color_index, float brightness, int neopxl_start, int neopxl_end)  {
  int8_t i = NEOPXL_COUNT;
  uint8_t r, g, b;

  #ifdef FL_DEBUG_MSG
      Serial.print("In neopxl_color_palette_set: color_index = ");Serial.println(color_index);
  #endif 
  r = rgb_palette[color_index][0] * brightness;
  g = rgb_palette[color_index][1] * brightness;
  b = rgb_palette[color_index][2] * brightness;
    
  for(i = neopxl_start; i <= neopxl_end; i++)  {
    strip.setPixelColor(i, r, g, b); 
  }
  #ifdef FL_DEBUG_MSG
      Serial.println("In neopxl_color_palette_set: done setting colors");
  #endif 
  
  strip.show();
  delay(NEOPXL_WAIT);

  #ifdef FL_DEBUG_MSG
      Serial.println("In neopxl_color_palette_set: after .show()");
  #endif 
}

/*
 * Neopixel VU mode
 */
#define NEOPXL_MODE_VU     2  /* play the current out on subset of pixels as vu meter */
#define NEOPXL_VU_START   15  /* starting pixel for VU mode */
#define NEOPXL_VU_END     29  /* ending pixel for VU mode */


struct vu_meter {
  uint8_t r_on;  /* rgb for on state */
  uint8_t g_on;
  uint8_t b_on;
  uint8_t r_off; /* rgb for off state */
  uint8_t g_off;
  uint8_t b_off;
};

/*
 * define the on/off colors for each pixel
 * NOTE: setup for exactly 15 pixels (i.e. half of a 30 pixel strand)
 */
#define NEOPXL_VU_MAX 15
struct vu_meter neopixel_vu[15]  {
  {  0, 127, 0,    0, 15, 0 }, /* 0 : green */
  {  7, 127, 0,    3, 15, 0 }, /* 1 */
  { 15, 127, 0,    6, 15, 0 }, /* 2 */
  { 31, 127, 0,    9, 15, 0 }, /* 3 */
  { 63, 127, 0,   12, 15, 0 }, /* 4 */
  {127, 127, 0,   15, 15, 0 }, /* 5 : yellow */
  {127,  63, 0,   15, 12, 0 }, /* 6 */
  {127,  31, 0,   15,  9, 0 }, /* 7 */
  {127,  15, 0,   15,  6, 0 }, /* 8 */
  {127,   0, 0,   15,  3, 0 }, /* 9 */
  {127,   0, 0,   15,  0, 0 }, /* 10 : red */
  {127,   0, 0,   15,  0, 0 }, /* 11 */
  {127,   0, 0,   15,  0, 0 }, /* 12 */
  {127,   0, 0,   15,  0, 0 }, /* 13 */
  {127,   0, 0,   15,  0, 0 }, /* 14 */
};

/*
 * set the vu meter according to the integer signal level color_index
 * 
 * if the pixel index is less than or equal to the signal, set it to the on
 * (expected to be brighter) state.
 * if the pixel index is greater than the signal, set it to the off/dimmer state.
 * 
 * need i to represent the pixel number (not starting at 0), and k to index the
 * the brightness/color array (starting at 0).
 */
void neopxl_vu_set(int color_index)  {

  uint8_t r, g, b;
  int i, k;

  for(i = NEOPXL_VU_START, k = 0; i <= NEOPXL_VU_END; i++, k++)  {
    /*
     * set all pixels below the signal level to on/brighter state
     */
    if(k <= color_index)  {
      r = neopixel_vu[k].r_on;
      g = neopixel_vu[k].g_on;
      b = neopixel_vu[k].b_on;
    }
    /*
     * set all pixels above the signal level to the off/dimmer state
     */
    else  {
      r = neopixel_vu[k].r_off;
      g = neopixel_vu[k].g_off;
      b = neopixel_vu[k].b_off;
    }
#ifdef FL_DEBUG_MSG
    Serial.print("k =  "); Serial.print(k);
    Serial.print(" r:");Serial.print(r);
    Serial.print(" g:");Serial.print(g);
    Serial.print(" b:");Serial.println(b);
    Serial.print("Setting pixel ");Serial.println(i);
#endif

    strip.setPixelColor(i, r, g, b);
  }
  strip.show();
  delay(NEOPXL_WAIT);
}

#endif // of NEOPIXELS

/* 
 *  NIST NETWORK TIME
 *  -----------------
 *  seconds to get from Greenwich to CST
 */
#define TZ_OFFSET      -21600

#ifdef WIFI_ON
// Instantiate the UDP and Network Time Protocol
WiFiUDP ntpUDP;  
NTPClient timeClient(ntpUDP);
long wifi_rssi;
#endif

/* 
 * HTU21D Temp/Humidity (I2C)
 * --------------------------
 * Temp and Humidity calibration
 * I measured the temp at 72, 44, 27 deg F.  The sensor was
 * about the same and 1.41 deg C  too high on average.  I don't have
 * a good way to measure humidity.
 */

#ifdef HTU21DF_P
// Instantiate the temp/humidity sensor
Adafruit_HTU21DF htu = Adafruit_HTU21DF();
#endif

/*
 *  SSR
 *  ---
 */
uint8_t  ssr_state = LOW;   /* the current state of the SSR: HIGH (0x01) = on, LOW (0x00) = off */
uint8_t  nssr_state = LOW;  /* as received from mqtt */
bool     ssr_state_valid = false; /* has a valid value been received via mqtt */

/*
 * GARAGE DOOR SENSE
 * -----------------
 */
#define OPEN   1
#define CLOSED 0
uint8_t gdoor_state;

/*
 * Other I/O
 */

uint8_t cncrtr_estop;


/*
 * WIFI
 */
#ifdef WIFI_ON
// Instantiate an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient WiFiclient;


/* 
 *  Connect to WiFi access point.
 *  Put it in a function in case re-connect is necessary.
 */
wl_status_t  LWifiConnect(bool first)  {

  int i = 0;
  wl_status_t wifi_status;
  
  Serial.println();
  
  /*
   * If this is the first time, do some initiation steps and wait
   * longer for the connect before returning status
   */
  if(first == true)  {
    Serial.print("Connecting to ");
    Serial.println(pmon_config->wlan_ssid);
    WiFi.begin(pmon_config->wlan_ssid, pmon_config->wlan_pass);
    
    // Wait for the connection to succeed
    while ((WiFi.status() != WL_CONNECTED) && (i++ < INITIAL_WIFI_WAIT)) {
      delay(500);
      Serial.print(".");
    }
  }
  
  /*
   * If this is a retry, return after one try
   */
  else  {
    Serial.print("Re-connecting to ");
    Serial.println(pmon_config->wlan_ssid);
  }

  Serial.println();
  if((wifi_status = WiFi.status()) == WL_CONNECTED)  {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
  
  return(wifi_status);
}
#endif

/*
 * MQTT
 * ----
 */

#ifdef WIFI_ON
// Setup the MQTT client class by passing in the WiFi client and MQTT server
PubSubClient mqtt(WiFiclient);

#endif // WIFI_ON


// variables for the raw sensor readings
float    temp;
float    humidity;
int16_t  gas;
String   enviro, timestamp;  /* pointer to the MQTT payload */


/*
 * ***************  SETUP  ***************
 */
void setup() {
  int i = 0;
  char inChar;
  char inbuf[64];
  bool out = false;
  uint16_t mqtt_port;


  Serial.begin(115200);  
  Serial.println("Starting ...");

  #ifdef ARDUINO_ESP8266_WEMOS_D1MINI
    Serial.println("Board is ESP8266_WEMOS_D1MINI");
  #else
    Serial.println("Board is *NOT* ESP8266_WEMOS_D1MINI");
  #endif

  
  /*
   * Setup the direction of the three hardwired 3.3V I/O pins (hardwired as above)
   */
  init_pins();

#ifdef MOM_SWITCH_P
  /*
   * do the extra setup per configuration
   */
  attachInterrupt(digitalPinToInterrupt(MOM_SWITCH), mom_switch_int, FALLING);
#endif

  /* note: if the interrupt is not attached, as above, 
   * the momentary switch code will remain dormant.
   */
  config_sw_pressed = false;

  /*
   * do the common gpio setup extra's
   */
  digitalWrite(ACQ_ACTIVE,false);

  /*
   * initialize the EEPROM and 
   * get a pointer to the mon_config structure (see bt_eepromlib).
   * try to keep this to read access.
   */
   eeprom_begin();
   pmon_config = get_mon_config_ptr();
    
  /*
   * sync the input prompt/label array of structures with mon_config
   */
/*  init_eeprom_input(); */
  
  Serial.println("Press any key to change settings");

  i = 5;
  out = false;
  while((out == false) && (i > 0))  {
    Serial.print(i);Serial.print(" . ");
  // check for incoming serial data:
    if (Serial.available() > 0) {
      // read incoming serial data:
      inChar = Serial.read();
      out = true;
    }
    delay(1000);
    i--;
  }
  Serial.println();


  /*
   * if the user entered a character and caused the above
   * while() to exit before the timeout, prompt the user to 
   * enter new network and mqtt configuration data
   * 
   * present previous, valid data from EEPROM as defaults
   */
  if(out == true)  {
    if(eeprom_validation(EEPROM_VALID) == 0)  {
      eeprom_get();  /* if the EEPROM is valid, get the whole contents */
      Serial.println();
      dispall_eeprom_parms();
    }

    /*
     * run the prompt/input sequenct to get the eeprom changes
     */
    getall_eeprom_inputs();

    Serial.println();
    dispall_eeprom_parms();
    Serial.print("Press any key to accept, or reset to correct");
    while(Serial.available() <= 0);
    Serial.read();
    Serial.println();
    
    /*
     * if agreed, write the new data to the EEPROM and use it
     */
    if(eeprom_validation(EEPROM_VALID) == 0)
      Serial.print("EEPROM: previous data exists ... ");
    else
      Serial.print("EEPROM data never initialized ... ");
      
    Serial.print("overwrite with new values? ('y' or 'n'):");
    out = false;
    do {
      l_read_string(inbuf, sizeof(inbuf), true);
      if(strcmp(inbuf, "y") == 0)
        out = true;
      else if (strcmp(inbuf, "n") == 0)
        out = true;
      else  {
        Serial.println();
        Serial.print("EEPROM data valid ... overwrite with new values? ('y' or 'n'):");
      }
    } while(out == false);
    Serial.println();

    /*
     * write the data to EEPROM if an affirmative answer was given
     */
    if(strcmp(inbuf, "y") == 0)  {
      Serial.println("Writing data to EEPROM ...");
      strcpy(pmon_config->valid, EEPROM_VALID);
      eeprom_put();
    }
  } /* entering new data */
  
  if(eeprom_validation(EEPROM_VALID) == 0)  {
    eeprom_get();
    Serial.println("EEPROM data valid ... using it");
    dispall_eeprom_parms();
  }
  else  {
    Serial.println("EEPROM data NOT valid ... reset and try enter valid data");
    Serial.read();
  }

  /* 
   * now that we know the debug level set in EEPROM,
   * let it locally
   */
  Serial.println();
  Serial.print("Setting debug level to ");Serial.println(atoi(pmon_config->debug_level));
  l_debug_set(atoi(pmon_config->debug_level));

  /*
   * initialize the graphics touchscreen if present
   * if screen calibration was requested, do it
   */
#ifdef GFX_DISP

  init_touchscreen(out);

#endif

  /*
   * precalculate some constants for the thermistor calculation
   * NOTE: This is expected to be after the EEPROM contents have
   * been copied to the local structure.
   */
  init_therms(therms);
  
#ifdef WIFI_ON
  /* 
   * Setup the LED for indicating Wifi connection and
   * connect to WiFi access point.
   */
  digitalWrite(WIFI_LED, WIFI_LED_DIS);
  if(LWifiConnect(true) == WL_CONNECTED)
    digitalWrite(WIFI_LED, WIFI_LED_CON);

  // Start the NTP client
  timeClient.begin();
  timeClient.setTimeOffset(atoi(pmon_config->tz_offset_gmt));

  // Get the first update from the NIST server
  if(timeClient.update() == true)
    Serial.println("NIST provided the time successfully");
    
  // Finish setting up the mqtt class instance now that I know everything
  mqtt_port = atoi(pmon_config->mqtt_server_port);
  mqtt.setServer(pmon_config->mqtt_server, mqtt_port);
  mqtt.setCallback(callback);
//  mqtt.setClient(WiFiclient);
  
  // Setup the MQTT connection and attempt an initial publish
  LMQTTConnect(true, pmon_config->mqtt_server, pmon_config->mqtt_location);

  /*
   *  subscribe to the topics specified above and
   *  sync up other values with the broker
   */
  if (mqtt.connected())  {
    /*
     * subscribe to the topics specified in the structure above
     */
    MQTT_Subscribe();

#ifdef NEOPIXELS
    /*
     * Synchronize with the data broker by publishing the local
     * values of neopixel mode and neopixel range
     * (couldn't figure out a way to force a read on subscribe() )
     * NOTE: format is simpler, cant use json_sample()
     * "{ "neopxl_mode":{ "value": 1}}"
     * "{"neopxl_range":{"value":8}}"
     * 
     */
  
    enviro =          String("{ \"neopxl_mode\":");
    enviro = enviro +   String("{ \"value\": ") + String(neopxl_mode) + String("}");
    enviro = enviro + String("}");
     
#ifdef L_DEBUG_MSG
    Serial.print("Sending neopxl_mode in setup: ");
#endif
      
    if (mqtt.publish(TOPIC_NEOPXL_MODE_INIT, (char*) enviro.c_str()))
#ifdef L_DEBUG_MSG
      Serial.println("Publish ok")
#endif
      ;
    else
#ifdef L_DEBUG_MSG
      Serial.println("Publish failed")
#endif
      ;
      
    enviro =          String("{ \"neopxl_color_max\":");
    enviro = enviro +   String("{ \"value\": ") + String(neopxl_color_max) + String("}");
    enviro = enviro + String("}");
    
#ifdef L_DEBUG_MSG
    Serial.print("Sending neopxl_color_max in setup: ");
#endif
      
    if (mqtt.publish(TOPIC_NEOPXL_RANGE_INIT, (char*) enviro.c_str()))
#ifdef L_DEBUG_MSG
      Serial.println("Publish ok")
#endif
      ;
    else
#ifdef L_DEBUG_MSG
      Serial.println("Publish failed")
#endif
      ;
#endif /* NEOPIXELS */
  } /* mqtt connected */
#endif /* WIFI_ON */

#ifdef HTU21DF_P
  // Setup the temp and humidity sensor
  Serial.println("HTU21D-F initialization");
  if (htu.begin() != true)
    Serial.println("Couldn't find t/h sensor!");
#endif

#ifdef ADS1015_P
  // Setup the ADC for acquisition
  Serial.println("Connecting to ADC");
  ads.begin();
  ads.setGain(ADC_GAIN); // Set the gain for all channels

  /*
   * fill up the array for running average with zero's
   */
  for (i = 0; i < ADC_CHANNELS; i++)  {
    for(j = 0; j < ADC_AVG_SAMPLES; j++)  {
      adc[i][j] = (float)0;
    }
  }
#endif

#ifdef BME680

  if (bme.begin() != true) {
    Serial.println("Could not find a valid BME680 sensor!");
  }
  else  {
    // Set up oversampling and filter initialization
    Serial.println("Setting up BME680");
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C for 150 ms
  }
  
#endif

#ifdef NEOPIXELS
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  delay(500);
  //strip.setBrightness(127); // out of 255
  neopxl_color_palette_set(1, NEOPXL_BRT, 0, (NEOPXL_COUNT-1));  /* green */
  delay(500);
  neopxl_color_palette_set(0, NEOPXL_BRT, 0, (NEOPXL_COUNT-1));  /* off */
#endif

  /*
   * precalculate the ACS758 offset calibration so as not
   * to waste time in the fast loop
   */
  acs758_offset = atof(pmon_config->acs758_offset)/(float)1000;

  currentMillis = previousMillis = millis();
  
  /*
   * initialize the slower loop timer for data collection/publish
   */
  sampleTimerOccured = false;   
  os_timer_setfn(&sampleTimer, sampleTimerCallback, NULL);  /* attach the callback */
  os_timer_arm(&sampleTimer, MQTT_INTERVAL, true);

  /*
   * initialize the faster, acquisition loop timer for data collection/publish
   */
  acqTimerOccured = false;   
  os_timer_setfn(&acqTimer, acqTimerCallback, NULL);  /* attach the callback */
  os_timer_arm(&acqTimer, ACQ_INTERVAL, true);
}

/*
 * ***************  LOOP  ***************
 */
void loop() {

  /*
   * check for the faster, acquisition time
   * at this writing, the loop is taking about 190 mS
   * 
   */
  if(acqTimerOccured == true)  {
    /*
     * keep track of milliseconds used in the fast loop
     */
    previousMillis = millis();

#ifdef GFX_DISP
    touchWasTouched();
#endif

    /*
     * Reminder, the MOM_SWITCH is on an interrupt:
     * config_sw_pressed is set by mom_switch_int()
     * when the interrupt is triggered on pin MOM_SWITCH
     */
    
    /*
     * ACQUIRE AND CONVERT ALL OF THE DATA TO PHYSICAL UNITS
     */ 
  #ifdef ADS1015_P
    /*
     * read through the adc  channels, averaging the number of
     * acquitisions per sample  (noise filtering)
     * 
     * set the output ACQ_ACTIVE high at start, low at end of acq cycle
     */

    digitalWrite(ACQ_ACTIVE,true); /* for monitoring timing with scope */
    
    for (i = 0; i < ADC_CHANNELS; i++)  {   /* loop through the adc channels */
      /*
       * speed things up if we're not averaging samples per reading
       * (different than running average, which always happens)
       */
      if(ADC_SAMPLES == 1)
        adc[i][ADC_AVG_SAMPLES-1] = ads.readADC_SingleEnded(i); /* put it in the last running average slot */
      else  {
        adcsum = 0;
        for(j = 0; j < ADC_SAMPLES; j++)  {  /* make the requested number of samples in the average */
          adcsum += ads.readADC_SingleEnded(i);
          delay(ADC_INTERVAL);
        }
        adc[i][ADC_AVG_SAMPLES-1] = round(adcsum / ADC_SAMPLES); /* put it in the last running average slot */
      }
#ifdef FL_DEBUG_MSG
      Serial.print("Instantaneous ADC["); Serial.print(i); Serial.print("] = ");
      Serial.println(adc[i][ADC_AVG_SAMPLES-1]);
#endif

    }


    /*
     * calculate the running average
     * 
     * NOTE: coming out of this the last and second last slot have the
     * same value in anticipation of the next time around
     * 
     */
    for (i = 0; i < ADC_CHANNELS; i++)  {
      adcsum = 0;  /* reusing the adcsum accumulator */
      for(j = 0; j < ADC_AVG_SAMPLES; j++)  {
        adcsum += adc[i][j]; /* add all of the saved samples into the accumulator */
        if(j > 0)  /* if not the first one, move left, getting ready for the next time */
          adc[i][j-1] = adc[i][j];
    #ifdef FL_DEBUG_MSG
      Serial.print("Instantaneous ADC["); Serial.print(i); Serial.print("]");
      Serial.print("["); Serial.print(j); Serial.print("] = ");
      Serial.println(adc[i][j]);
    #endif
      }
      adc_ravg[i] = round(adcsum / ADC_AVG_SAMPLES);
    #ifdef FL_DEBUG_MSG
      Serial.print("Average ADC["); Serial.print(i); Serial.print("] = "); Serial.println(adc_ravg[i]);
    #endif
    }

  
  #endif
  
  #ifdef THERMISTORS
    /*
     * Convert the raw thermistor voltage (adc input) to
     * physical temperature values.
     * 
     * Using the running average since temperature changes slowly
     * 
     */
    for(i = 0; i < THERM_CNT; i++)  {
    #ifdef FL_DEBUG_MSG
      Serial.print("Thermistor # "); Serial.print(i); Serial.println(" :");
    #endif
      therms[i].thermR = seriesR / ((adcMax/adc_ravg[therms[i].adc_ch]) - 1);
    #ifdef FL_DEBUG_MSG
      Serial.print("thermR: "); Serial.print(therms[i].thermR);
    #endif
      therms[i].tempK = 1.00 / 
         (therms[i].invT0 + therms[i].invBeta * (log(therms[i].thermR/therms[i].calT0R)));
    
      therms[i].tempC = therms[i].tempK - 273.15;
      therms[i].tempF = ((9.0 * therms[i].tempC) / 5.00) + 32.00;
    #ifdef FL_DEBUG_MSG
      Serial.print(", tempK: "); Serial.print(therms[i].tempK);
      Serial.print(", tempC: "); Serial.print(therms[i].tempC);
      Serial.print(", tempF: "); Serial.println(therms[i].tempF);
    #endif
    }
    
  #endif

  #if (INA169 || ACS758)
    /*
     * convert the above read/calculated a/d reading to a current reading in amps
     * 
     * Calculate using the running average for the mqtt publish, and, 
     * the instantaneous for the neopixel output
     */
    Iamps = (*current_bits_to_amps)(adc[CURRENT_ADCCH][ADC_AVG_SAMPLES-1]);
    Aamps = (*current_bits_to_amps)(adc_ravg[CURRENT_ADCCH]);
    
    #ifdef FL_DEBUG_MSG
      Serial.print("Aamps = "); Serial.println(Aamps);
    #endif
  #endif

  #ifdef NEOPIXELS

    /*
     * based on the neopixel_mode (defined above),
     * adjust the neopixels color/level appropriately.
     * neopxl_mode is set in the slower loop
     */
    switch(neopxl_mode)  {

    case NEOPXL_MODE_OFF:
      /*
       * turn neopixels off by writing 0 to all
       */
      neopxl_color_palette_set(0, 0, NEOPXL_COLOR_START, NEOPXL_COLOR_END);
      break;
      
    case NEOPXL_MODE_COLOR:
    
      /*
       * write the current reading to a color on the neopixel strip
       * remember: map() can map beyond its limits, hence, constrain()
       */
      i = constrain(
        map(Iamps, 0, neopxl_color_max, 1, NEOPXL_PAL_MAX),
        0, NEOPXL_PAL_MAX);

      neopxl_color_palette_set(i, NEOPXL_BRT, NEOPXL_COLOR_START, NEOPXL_COLOR_END);
      break;

    case NEOPXL_MODE_VU:
      i = constrain(
        map(Iamps, 0, neopxl_color_max, 0, NEOPXL_VU_MAX),
        0, NEOPXL_VU_MAX);
#ifdef FL_DEBUG_MSG
      Serial.println("neopxl mode NEOPXL_MODE_VU executing");
      Serial.print("  level = ");Serial.println(i);
#endif
      neopxl_vu_set(i);
      break;
      
    default:
      break;
    }

  #endif

#ifdef ESTOP
    /*
     * read the estop digital input
     */
    cncrtr_estop = digitalRead(ESTOP_PIN);
#endif

#ifdef GDOOR_SENSE
    /*
     * read the garage door digital input
     */
    gdoor_state = digitalRead(GDOOR_PIN);

#endif

    /*
     * how much time used in theloop
     */
    currentMillis = millis();
  #ifdef FL_DEBUG_MSG
    Serial.print("Time used in loop: "); Serial.println(currentMillis - previousMillis);
    Serial.println();
  #endif
    acqTimerOccured = false;

    
    digitalWrite(ACQ_ACTIVE,false); /* for monitoring timing with scope */
    
  }  /* end of FAST acquisition loop */



  /*
   * check for the publish/SLOWER TICK TIMER
   */
  if(sampleTimerOccured == true)  {


    Serial.println("Slow Tick Occured");
    
#ifdef WIFI_ON  
    /*
     * Status the WIFI and set the LED indicator accordingly.
     * Reset the device on repeated, continuous WiFi failures
     * (just try again if the RST_ON_WIFI_FAIL is not set)
     */
    if(WiFi.status() == WL_CONNECTED)  {
      digitalWrite(WIFI_LED, WIFI_LED_CON);
      wifi_fails = RST_ON_WIFI_COUNT;  /* reset the fail counter */
      wifi_rssi = WiFi.RSSI();
      Serial.print("Wifi signal strength = ");
      Serial.print(wifi_rssi);
      Serial.println("dBm");
    }
    else {
      digitalWrite(WIFI_LED, WIFI_LED_DIS);  /* indicate that the wifi is down */
      
      /*
       * if the reset-on-wifi-fail is enabled, 
       * check if we'll be resetting the cpu first.
       * (no reason to reconnect to attempt wifi connect if so)
       */
      if(RST_ON_WIFI_FAIL)  {
        if(--wifi_fails > 0)
          LWifiConnect(false);
        else  {
          Serial.println("Resetting in 2 seconds because of WiFi fail");
          delay(2000);
          ESP.restart();
        }
      }
      else
        LWifiConnect(false);
    }
  
    // Service the NTP client. Updates happen much slower per defaults.
    timeClient.update();
    
#endif /* of WIFI status/reset section */

#ifdef LOCAL_SSR

    /*
     * has a value for the ssr been received from the 
     * mqtt-subscribed topic ?
     * 
     * change the state if a new value was received
     * (I don't know if there would be a glitch in the SSR
     *  output if it's written every time, so only write it
     *  if it needs to change.)
     */
    get_parm_valid(TOPIC_SSR_STATE, &ssr_state_valid);
#ifdef FL_DEBUG_MSG
    Serial.print("ssr_state_valid: "); Serial.println(ssr_state_valid);
#endif
    if(ssr_state_valid == true) {
      parm_to_value(TOPIC_SSR_STATE, &nssr_state);
#ifdef FL_DEBUG_MSG
      Serial.print("ssr_state: "); Serial.println(ssr_state);
      Serial.print("nssr_state: "); Serial.println(nssr_state);
#endif
      if(nssr_state != ssr_state)  {
        ssr_state = nssr_state;
        Serial.print("Setting new ssr_state to: ");Serial.println(ssr_state);
        digitalWrite(SSR_PIN, ssr_state);
      }
    }

    
#endif


#ifdef NEOPIXELS

   /*
    * first, see if a value for the neopixel mode has been
    * received from mqtt.  If so, and it's different, set the
    * active variable, neopxl_mode, to that value.
    * 
    * If not, continue to use the default value.
    */
    get_parm_valid(TOPIC_NEOPXL_MODE, &neopxl_mode_valid);
    if(neopxl_mode_valid == true) {
      parm_to_value(TOPIC_NEOPXL_MODE, &nneopxl_mode);
      if(nneopxl_mode != neopxl_mode)  {
        neopxl_mode = nneopxl_mode;
        Serial.print("Setting new neopxl_mode to (from mqtt):");Serial.println(neopxl_mode);
        neopxl_color_palette_set(0, 0, 0, (NEOPXL_COUNT-1));
      }
    }

    /*
     * if the momentary contact switch was pressed (caught by the ISR)
     * increment to the next neopixel mode.  Roll back to start if max'ed.
     * Note: if you change the mode from nodered and you
     *       press the momentary contact switch to change the mode
     *       in the same 3 second window,
     *       the mode will be first set to the new mqtt mode (just above)
     *       and then incremented.
     * 
     * Reminder, the MOM_SWITCH is on an interrupt:
     * config_sw_pressed is set by mom_switch_int()
     * when the interrupt is triggered on pin MOM_SWITCH
     *
     */
    if(config_sw_pressed == true)  {
      config_sw_pressed = false;
      if(neopxl_mode < NEOPXL_MODE_MAX)
        neopxl_mode++;
      else
        neopxl_mode = NEOPXL_MODE_MIN;
      Serial.print("Setting new neopxl_mode to (from mom_switch) : ");Serial.println(neopxl_mode);
      neopxl_color_palette_set(0, 0, 0, (NEOPXL_COUNT-1));


#ifdef WIFI_ON
      /*
       * send the new neopixel mode up to nodered,
       * which will be echo'ed back as the new mode;
       * new and old will match above.
       */
      if (mqtt.connected())  {
        /*
         * Synchronize with the data broker by publishing the local
         * values of neopixel mode
         */
      
        enviro =          String("{ \"neopxl_mode\":");
        enviro = enviro +   String("{ \"value\": ") + String(neopxl_mode) + String("}");
        enviro = enviro + String("}");
     
#ifdef L_DEBUG_MSG
        Serial.print("Sending neopxl_mode in loop: ");
#endif
      
        if (mqtt.publish(TOPIC_NEOPXL_MODE_INIT, (char*) enviro.c_str()))
#ifdef L_DEBUG_MSG
          Serial.println("Publish ok")
#endif
          ;
        else
#ifdef L_DEBUG_MSG
          Serial.println("Publish failed")
#endif
        ;
      }
#endif // of WIFI_ON
    }
    
    /*
     * see if a new value for the top of the current/color range
     * has been received from mqtt.  If so, set it.
     */
    get_parm_valid(TOPIC_NEOPXL_RANGE, &neopxl_mode_valid);
    if(neopxl_mode_valid == true)
      parm_to_value(TOPIC_NEOPXL_RANGE, &neopxl_color_max);

#endif  // of NEOPIXELS



    /*
     * read the physical sensors, if they exist
     * to optimize the speed of the a/d, this is only
     * read in the slow loop
     */
  #ifdef HTU21DF_P
    temp = htu.readTemperature() + atof(pmon_config->temp_offset);
    humidity = htu.readHumidity() + atof(pmon_config->hum_offset);
  #endif

  #ifdef BME680
    if (bme.performReading() != true) {
      Serial.println("Failed to perform reading :(");
    }
#ifdef FL_DEBUG_MSG
    else  {
      l_debug("Temperature = ", 5);
      l_debug(bme.temperature, 5);
      l_debugln(" *C", 5);
    
      l_debug("Pressure = ", 5);
      l_debug(bme.pressure / 100.0, 5);
      l_debugln(" hPa", 5);
    
      l_debug("Humidity = ", 5);
      l_debug(bme.humidity, 5);
      l_debugln(" %", 5);
    
      l_debug("Gas = ", 5);
      l_debug(bme.gas_resistance / 1000.0, 5);
      l_debugln(" KOhms", 5);
    
      l_debug("Approx. Altitude = ", 5);
      l_debug(bme.readAltitude(SEALEVELPRESSURE_HPA), 5);
      l_debugln(" m", 5);
    }
#endif
  #endif  /* of BME680 */

  #ifdef MICS5524_P

  #ifdef FL_DEBUG_MSG
    Serial.print("Just before gas calc adc_ravg[0] = "); Serial.println(adc_ravg[0]);
    Serial.print("Just before gas calc adc_ravg[1] = "); Serial.println(adc_ravg[1]);
  #endif
  
    gas = adc_ravg[0] - adc_ravg[1];
  #endif

    /*
     * END OF ACQUISITION
     */

  #ifdef WIFI_ON
    /*
     * PUBLISH ALL OF THE DATA VIA MQTT
     */
    if (mqtt.connected()) {
      mqtt_fails = RST_ON_MQTT_COUNT; // reset the counter in case it was counting down
      
      /*
       * Prepare and send the sample time ... do this first to be close to the acquition.
       */
      timestamp = timeClient.getFormattedTime();  // leave this outside the ifdef incase it will be used

#ifdef SEND_TSTAMP  
      enviro = json_sample("tstamp", timestamp, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending sample time: ");
    #endif
      if (mqtt.publish(TOPIC_STIME, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;

#endif


      /*
       * prepare and send the WIFI RSSI (i.e. signal strength)
       */
      enviro = json_sample("rssi", wifi_rssi, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending rssi: ");
    #endif
      if (mqtt.publish(TOPIC_WIFI_RSSI, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;

       
  #ifdef HTU21DF_P
  
      enviro = json_sample("temp", temp, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-temp data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_TEMP, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  
  
      /*
       * prep and send the humidity data
       */
       
      enviro = json_sample("humidity", humidity, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-hum data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_HUM, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  #endif
  
  #ifdef BME680

      /*
       * prep and send the temp data
       */
      enviro = json_sample("temp", float(bme.temperature+atof(pmon_config->temp_offset)), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-temp data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_TEMP, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSGtemp
        Serial.println("Publish failed")
      #endif
        ;

      /*
       * prep and send the humidity data
       */      
      enviro = json_sample("humidity", float(bme.humidity+atof(pmon_config->hum_offset)), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-hum data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_HUM, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;

      /*
       * prep and send the pressure data
       */      
      enviro = json_sample("pressure", float(bme.pressure/100.0), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-pressure data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_PRES, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;

      /*
       * prep and send the calculated altitude data
       */      
      enviro = json_sample("altitude", float(bme.readAltitude(SEALEVELPRESSURE_HPA)), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-altitude data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_ALT, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;


      /*
       * prep and send the calculated altitude data
       */      
      enviro = json_sample("gasR", float(bme.gas_resistance / 1000.0), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-gasR data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_GASR, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;

  #endif  /* of BME680 */
  
  #ifdef THERMISTORS
      enviro = json_sample("therm0", therms[0].tempC, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-therm0 data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_THERM0, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
        
      enviro = json_sample("therm1", therms[1].tempC, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-therm1 data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_THERM1, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  #endif

  #if INA169 || ACS758
      enviro = json_sample("Aamps", Aamps, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending Aamps data: ");
    #endif
    
    if (mqtt.publish(TOPIC_ENV_AAMPS, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
    else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;      
  #endif
  
  #ifdef MICS5524_P
      /*
       * send the raw gas sensor data
       */
      enviro = json_sample("gasrw", gas, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-gasrw data: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_GASRW, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  
      /*
       * send the gas sensor data converted to PPM as if CO
       */   
      enviro = json_sample("gasco", gas_v_to_ppm(CARBMONO, gas), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-gasco data as Carbon Monoxide PPM: ");
    #endif
      
      if (mqtt.publish(TOPIC_ENV_GASCO, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  
      /*
       * send the gas sensor data converted to PPM as if Propane
       */
      enviro = json_sample("gaspr", gas_v_to_ppm(PROPANE, gas), pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending enviro-gaspr data as Propane PPM: ");
    #endif
      if (mqtt.publish(TOPIC_ENV_GASPR, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  #endif

  #ifdef ESTOP
      /*
       * send the estop pin status
       */
      enviro = json_sample("estop", cncrtr_estop, pmon_config->mqtt_location, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending cncrtr_estop data ");
    #endif
      if (mqtt.publish(TOPIC_ENV_ESTOP, (char*) enviro.c_str()))
      #ifdef L_DEBUG_MSG
        Serial.println("Publish ok")
      #endif
        ;
      else
      #ifdef L_DEBUG_MSG
        Serial.println("Publish failed")
      #endif
        ;
  #endif

  #ifdef GDOOR_SENSE
      /*
       * send the garage door pin status
       */
      enviro = json_sample("gdoor_state", gdoor_state, pmon_config->mqtt_location, timestamp);

      l_debug("Sending gdoor_state data ", 3);
      l_debug("(", 5);
      l_debug(gdoor_state, 5);
      l_debug(") ", 5);

      if (mqtt.publish(TOPIC_ENV_GDOOR, (char*) enviro.c_str()))

        l_debugln("Publish ok", 3);
      else
        l_debugln("Publish failed", 3);
  #endif
  
    } /* end of mqtt connect check */
    
    /*
     * if the mqtt connection was lost, attempt to reconnect
     */
    else {
      if(RST_ON_MQTT_FAIL)  {
        if(--mqtt_fails > 0)
          LMQTTConnect(false, pmon_config->mqtt_server, pmon_config->mqtt_location);
        else  {
          Serial.println("Resetting in 2 seconds because of MQTT fail");
          delay(2000);
          ESP.restart();
        }
      }
      else
        LMQTTConnect(false, pmon_config->mqtt_server, pmon_config->mqtt_location);

      /*
       * assume that the subscriptions were lost and re-subscribe
       */
      if (mqtt.connected()) {
        if(MQTT_Subscribe() == true)
          l_debugln("Successful re-subscribe after mqtt connection loss", 2);
      }
    }


    mqtt.loop();
  #endif  // of WIFI_ON
  /* of mqtt publish block */
  
    sampleTimerOccured = false;
    
#ifdef L_DEBUG_MSG
    Serial.println();
    Serial.println("System monitoring:");
    Serial.print("ESP.getBootMode(); ");
    Serial.println(ESP.getBootMode());
    Serial.print("ESP.getSdkVersion(); ");
    Serial.println(ESP.getSdkVersion());
    Serial.print("ESP.getBootVersion(); ");
    Serial.println(ESP.getBootVersion());
    Serial.print("ESP.getChipId(); ");
    Serial.println(ESP.getChipId());
    Serial.print("ESP.getFlashChipSize(); ");
    Serial.println(ESP.getFlashChipSize());
    Serial.print("ESP.getFlashChipRealSize(); ");
    Serial.println(ESP.getFlashChipRealSize());
    Serial.print("ESP.getFlashChipSizeByChipId(); ");
    Serial.println(ESP.getFlashChipSizeByChipId());
    Serial.print("ESP.getFlashChipId(); ");
    Serial.println(ESP.getFlashChipId());
    Serial.print("ESP.getFreeHeap(); ");
    Serial.println(ESP.getFreeHeap());
    Serial.println();
#endif

#ifdef GFX_DISP

  disp_update(timeClient.getEpochTime());

#endif

  } /* end of slow loop */

  yield();  /* let the wifi process run */
} /* end of loop */
