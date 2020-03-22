
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
 * 12/06/17 - 3/15/20
 * 
 * Firmware to acquire environmental parameters and send via mqtt to broker
 * 
 * Hardware:
 * Adafruit Huzzah ESP8266 module (for development; ESP-12S (AF P2491) target)
 * Adafruit HTU21D Temp/Humidity i2c module
 * Adafruit ADS1015 12-bit i2c ADC (P1083) (Host of thermistors and current sensing)
 * Adafruit MiCS5524 Gas Sensor (P3199) - no longer supported; see previous version (i2c support coming)
 * Adafruit INA169 Analog DC Current Sensor Breakout - 60V 5A Max (P1164)
 * 
 * Boards and Libraries:
 * - In Arduino IDE->File->Preferences, add this to the "Additional Board Managaer URLs" field:
 * http://arduino.esp8266.com/stable/package_esp8266com_index.json
 * - Go to the Tools->Board->Board Manager ... (at the top of the board selection list)
 * - Install "esp8266 by ESP8266 Community"
 * 
 * In Tools->Manage Libraries ... :
 * - Install the Adafruit ADS1X15 by Adafruit
 * - Install Adafruit HTU21DF by Adafruit
 * - InstallPubSub Client by Nick O'Leary
 * - Install NTP Client by Fabrice Weinberg
 * - I may have missed one
 * 
 * I was building under IDE 1.6.7.  Upgraded to 1.8.10 and updated some libraries.  Still works
 * 
 * Pending:
 *
 * Now:
 * + potentially move the HTU21D read of temp and humidity to the slow loop
 * + include the topics in the thermistor structure ?
 * + blink the WIFI connected LED for heartbeat
 * + add neoPixel status indications
 * 
 * Longer term:
 * + add separate calibrations to thermistor structure
 * + add the location to the json string
 * + decide if there is a way to dynamically configure the network/hardware config
 * + add flash based config file (json) (e.g. MQTT server, timezone, etc.) ... use EEPROM lib
 * + if the NIST time sync fails, annotate the time with an asterisk
 * + implement a red "failed" light
 * + add the ability to reboot remotely from a data broker topic flag; maybe heartbeat
 * 
 * v1.3:
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
 * 
 */

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <Adafruit_HTU21DF.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include <user_interface.h>  /* for os timer functionality */
#include <Adafruit_NeoPixel.h>


/********************* Hardware Connections ********************/
// Note : currently no conflicts between the env sensor and the CNC version
//
//#define UNUSED    0  // built in LED on Huzzah
//#define UNUSED    2
//#define I2C_SDA   4  // defined elsewhere
//#define I2C_SCL   5  // defined elsewhere
#define WIFI_LED    12 // output pin to indicate WIFI connect status
#define ESTOP_PIN   13 // input to provide the status of the router estop
#define INPUT_14    14 // hardwired as an optically isolated input
#define NEOPXL_PIN  15 // output pin for neopixel data
//#define UNUSED    16

/* 
 *  which hardware is actually connected?
 *  note: you have to manage dependencies yourself
 *        (e.g. if no adc, thermistors and current not possible)
 */
#define WIFI_ON         // Should wifi be enabled; used for debug, not fully vetted
#define HTU21DF_P       // Is the temp/hum module present
#define ADS1015_P       // Is the A/D present
#define THERMISTORS  2  // Are we using the A/D to sense thermistors, and how many
#define INA169          // Is the current shunt present
#define NEOPIXELS       // neopixels are present


/********************* WiFi Access Point ***********************/

// home
#define WLAN_SSID       "ZEther-2G"
#define WLAN_PASS       "FAIL"
#define LOCATION        "Basement_Tech"

/*********************  MQTT Server Info  *********************/
//#define SERVER      "192.168.1.10"
//#define SERVERPORT  1883
#define SERVER        "192.168.1.24"
#define SERVERPORT    52065

/********************* MQTT Payload Information ***************/

// This section is used for the free air sensing module
//#define TOPIC_TEST       "PamTest-esp8266"
//#define TOPIC_ENV_TEMP   "zk-env/temp"
//#define TOPIC_ENV_HUM    "zk-env/humidity"
//#define TOPIC_ENV_GASCO  "zk-env/gasco"
//#define TOPIC_ENV_GASPR  "zk-env/gaspr"
//#define TOPIC_ENV_GASRW  "zk-env/gasrw"
//#define TOPIC_STIME      "zk-env/time"

// This section is used for the CNC Router sensing module
#define TOPIC_TEST       "PamTest-esp8266"
#define TOPIC_ENV_TEMP   "zk-cncrtr/temp"
#define TOPIC_ENV_HUM    "zk-cncrtr/humidity"
#define TOPIC_STIME      "zk-cncrtr/time"
#define TOPIC_ENV_THERM0 "zk-cncrtr/therm0"
#define TOPIC_ENV_THERM1 "zk-cncrtr/therm1"
#define TOPIC_ENV_AAMPS  "zk-cncrtr/aamps"
#define TOPIC_ENV_ESTOP  "zk-cncrtr/estop"


/********************* Behavioral Characteristics *************/

/*
 * timer intervals for the main sensing loop and publish
 */
#define MQTT_INTERVAL     3000 // mS between publish's
#define ACQ_INTERVAL      100  // A/D sampling interval (mS)

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

/*
 * set up the structure and callback() for the slower loop functionality
 */
os_timer_t sampleTimer; /* for the slower sample loop */
bool sampleTimerOccured;
void sampleTimerCallback(void *pArg)  {
  sampleTimerOccured = true;
}

/*
 * faster loop
 */
os_timer_t acqTimer; /* for the slower sample loop */
bool acqTimerOccured;
void acqTimerCallback(void *pArg)  {
  acqTimerOccured = true;
}
 
/*
 * for calculating loop timing during development
 */
unsigned long currentMillis = 0, previousMillis = 0;

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
 */
#define ADC_GAIN         GAIN_TWO /* +/- 2.048 V  */
#define ADC_CHANNELS     4  /* number of channels in the ADC */

#define ADC_SAMPLES      2  /* just for electrical noise filtering */
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
 */
const float adcMax = 3336.00;  /* more precise 3.3v, measured */
const float invBeta = 1.00 / 3950.00;  /* thermistor beta parameter from the manufacturer */
const float invT0 = 1.00 / (22.06 + 273.15);  /* 1/T0 in deg K, inverse of thermistor calibration point */
const float T0R = 107592.00;  /* thermistor resistance at T0  */
const float seriesR = 100000.00;  /* series resistor */

struct thermistor  {
  int adc_ch;
  float tempK;
  float tempC;
  float tempF;
  float thermR;
};


struct thermistor therms[THERMISTORS] =
{
  {1, 0, 0, 0, 0},
  {3, 0, 0, 0, 0}
};


/*
 * HIGH SIDE CURRENT MONITOR (INA169 based)
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
#define INA169_ADCCH 0  /* adc channel for ina169/shunt input */
const float INA169_shunt_R = 0.001;  /* Current measuring shund resistor */
const float INA169_load_R  = 68000;  /* Output load resistance */
const float INA169_Vgain   = INA169_load_R / (float)1000;
float       INA169_Aamps;  /* running average of current */

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

  rs = RL * ((VS/(VPBIT * bits)) - 1);
  
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
 *  NEOPIXELS
 *  ---------
 */
#define NEOPXL_COUNT  30
/* NEOPXL_PIN defined above in hardware section */
#define NEOPXL_BRT    (float)0.5 /* default neopixel brightness */
#define NEOPXL_WAIT   50  /* time to wait after .show() */


#ifdef NEOPIXELS
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NEOPXL_COUNT, NEOPXL_PIN, NEO_GRB + NEO_KHZ800);
#endif

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
 * 
 */
void neopxl_color_palette_set(int color_index, float brightness)  {
  int8_t i = NEOPXL_COUNT;
  uint8_t r, g, b;

  #ifdef FL_DEBUG_MSG
      Serial.print("In neopxl_color_palette_set: color_index = ");Serial.println(color_index);
  #endif 
  r = rgb_palette[color_index][0] * brightness;
  g = rgb_palette[color_index][1] * brightness;
  b = rgb_palette[color_index][2] * brightness;
    
  for(i = 0; i < NEOPXL_COUNT; i++)  {
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
 *  NIST NETWORK TIME
 *  -----------------
 *  seconds to get from Greenwich to CST
 */
#define TZ_OFFSET      -21600

#ifdef WIFI_ON
// Instantiate the UDP and Network Time Protocol
WiFiUDP ntpUDP;  
NTPClient timeClient(ntpUDP, TZ_OFFSET);
#endif

/* 
 * HTU21D Temp/Humidity (I2C)
 * --------------------------
 * Temp and Humidity calibration
 * I measured the temp at 72, 44, 27 deg F.  The sensor was
 * about the same and 1.41 deg C  too high on average.  I don't have
 * a good way to measure humidity.
 */
#define TEMP_OFFSET   -1.41
#define HUM_OFFSET     0

#ifdef HTU21DF_P
// Instantiate the temp/humidity sensor
Adafruit_HTU21DF htu = Adafruit_HTU21DF();
#endif

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
    Serial.println(WLAN_SSID);
    WiFi.begin(WLAN_SSID, WLAN_PASS);
    
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
    Serial.println(WLAN_SSID);
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

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message back from broker");
  Serial.println(topic);
}

// Convert a mac address to a string
String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

#ifdef WIFI_ON
// Setup the MQTT client class by passing in the WiFi client and MQTT server
PubSubClient mqtt(SERVER, SERVERPORT, callback, WiFiclient);


/*
 * Connect or re-connect to the mqtt data broker
 */
bool LMQTTConnect(bool first)  {
  
  bool status = false;
  
  // Generate client name based on MAC address and last 8 bits of microsecond counter
  String clientName;
  clientName += "esp8266-";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  clientName += macToStr(mac);
  clientName += "-";
  clientName += String(micros() & 0xff, 16);

  if(first == true)
    Serial.print("Attempting connection to MQTT ");
  else
    Serial.print("Attempting re-connection to MQTT ");
  Serial.print(SERVER);
  Serial.print(" as ");
  Serial.println(clientName);
  
  if (mqtt.connect((char*) clientName.c_str())) {
    Serial.println("Connected to MQTT broker");
    Serial.print("Topic is: ");
    Serial.println(TOPIC_TEST);
    
    if (mqtt.publish(TOPIC_TEST, "hello from ESP8266")) {
      Serial.println("Initial Publish ok");
    }
    else {
      Serial.println("Initial Publish failed");
      status = false;
    }
  }
  else {
    Serial.println("MQTT connect failed");
    Serial.println("Will try again...");
    status = false;
  }
  return(status);
}
#endif

void delay_yield(int msecs)  {
  int i = 0;
  for (i = 0; i < msecs/10; i++)  {
    delay(10);
    yield();
  }
}

/*
 * Form up a json payload.  Overload for three parm types.
 * (could have found a json library, but this seemed pretty trivial)
 */
String json_sample(String parm, float value, String location, String tstamp)  {
  
  String enviro;
  
  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": " + String(value) + ",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);
}
String json_sample(String parm, String value, String location, String tstamp)  {
  
  String enviro;
  
  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": \"" + value + "\",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);  
}
String json_sample(String parm, int value, String location, String tstamp)  {
  
  String enviro;
  
  enviro =          "{ \"" + parm + "\":";
  enviro = enviro +   "{ " + "\"value\": \"" + value + "\",";
  enviro = enviro +          "\"location\": \"" + location + "\",";
  enviro = enviro +          "\"tstamp\": \"" + tstamp + "\"";
  enviro = enviro +   "}";
  enviro = enviro + "}";

  return(enviro);  
}

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


  
  Serial.begin(115200);  

  /*
   * Setup the direction of the three hardwired 3.3V I/O pins (hardwired as above)
   */
  pinMode(NEOPXL_PIN, OUTPUT);
  pinMode(ESTOP_PIN, INPUT);
  pinMode(INPUT_14, INPUT);
  // fourth is undefined/unused at this writing
  
#ifdef WIFI_ON
  /* 
   * Setup the LED for indicating Wifi connection and
   * connect to WiFi access point.
   */
  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, HIGH);
  if(LWifiConnect(true) == WL_CONNECTED)
    digitalWrite(WIFI_LED, LOW);

  // Start the NTP client
  timeClient.begin();

  // Get the first update from the NIST server
  if(timeClient.update() == true)
    Serial.println("NIST provided the time successfully");
  
  // Setup the MQTT connection and attempt an initial publish
  LMQTTConnect(true);
#endif

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

#ifdef NEOPIXELS
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  delay(500);
  //strip.setBrightness(127); // out of 255
  neopxl_color_palette_set(1, NEOPXL_BRT);  /* green */
  delay(500);
  neopxl_color_palette_set(0, NEOPXL_BRT);  /* off */
#endif

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
    
    /*
     * ACQUIRE AND CONVERT ALL OF THE DATA TO PHYSICAL UNITS
     */ 
  #ifdef ADS1015_P
    /*
     * read through the adc  channels, averaging the number of
     * acquitisions per sample  (noise filtering)
     */
    for (i = 0; i < ADC_CHANNELS; i++)  {   /* loop through the adc channels */
      adcsum = 0;
      for(j = 0; j < ADC_SAMPLES; j++)  {  /* make the requested number of samples in the average */
        adcsum += ads.readADC_SingleEnded(i);
        delay(ADC_INTERVAL);
      }
      adc[i][ADC_AVG_SAMPLES-1] = round(adcsum / ADC_SAMPLES); // put it in the last running average slot
    #ifdef FL_DEBUG_MSG
      Serial.print("Instantaneous ADC["); Serial.print(i); Serial.print("] = "); Serial.println(adc[i][ADC_AVG_SAMPLES-1]);
    #endif
    }

    /*
     * calculate the running average
     */
    for (i = 0; i < ADC_CHANNELS; i++)  {
      adcsum = 0;
      for(j = 0; j < ADC_AVG_SAMPLES; j++)  {
        adcsum += adc[i][j];       
        if(j > 0)
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
    for(i = 0; i < THERMISTORS; i++)  {
    #ifdef FL_DEBUG_MSG
      Serial.print("Thermistor # "); Serial.print(i); Serial.println(" :");
    #endif
      therms[i].thermR = seriesR / ((adcMax/adc_ravg[therms[i].adc_ch]) - 1);
    #ifdef FL_DEBUG_MSG
      Serial.print("thermR: "); Serial.print(therms[i].thermR);
    #endif
      therms[i].tempK = 1.00 / (invT0 + invBeta * (log(therms[i].thermR/T0R)));
    
      therms[i].tempC = therms[i].tempK - 273.15;
      therms[i].tempF = ((9.0 * therms[i].tempC) / 5.00) + 32.00;
    #ifdef FL_DEBUG_MSG
      Serial.print(", tempK: "); Serial.print(therms[i].tempK);
      Serial.print(", tempC: "); Serial.print(therms[i].tempC);
      Serial.print(", tempF: "); Serial.println(therms[i].tempF);
    #endif
    }
    
  #endif

  #ifdef INA169
    /*
     * convert the above read/calculated running average a/d reading
     * to a current reading in amps
     */
    INA169_Aamps = INA169_bits_to_amps(adc_ravg[INA169_ADCCH]);
    #ifdef FL_DEBUG_MSG
      Serial.print("INA169_Aamps = "); Serial.println(INA169_Aamps);
    #endif
  #endif

  #ifdef NEOPIXELS
    /*
     * write the current reading to a color on the neopixel strip
     * NOTE: at this writing, I'm taking advantage of the fact that
     * the number of colors is about the same as the range of amps expected.
     */
    neopxl_color_palette_set(constrain((int)INA169_Aamps, 0, NEOPXL_PAL_MAX), NEOPXL_BRT);
  #endif

    /*
     * read the estop digital input
     */
    cncrtr_estop = digitalRead(ESTOP_PIN);

    /*
     * how much time used in theloop
     */
    currentMillis = millis();
  #ifdef FL_DEBUG_MSG
    Serial.print("Time used in loop: "); Serial.println(currentMillis - previousMillis);
    Serial.println();
  #endif
    acqTimerOccured = false;
  }  /* end of fast acquisition loop */

  /*
   * check for the publish/slower tick timer
   */
  if(sampleTimerOccured == true)  {


    Serial.println("Tick Occured");
#ifdef WIFI_ON  
    /*
     * Status the WIFI and set the LED indicator accordingly.
     * Reset the device on repeated, continuous WiFi failures
     * (just try again if the RST_ON_WIFI_FAIL is not set)
     */
    if(WiFi.status() == WL_CONNECTED)  {
      digitalWrite(WIFI_LED, LOW);
      wifi_fails = RST_ON_WIFI_COUNT;  // reset the fail counter
    }
    else {
      if(RST_ON_WIFI_FAIL)  {
        digitalWrite(WIFI_LED, HIGH);
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
#endif

    /*
     * read the physical sensors, if they exist
     * to optimize the speed of the a/d, this is only
     * read in the slow loop
     */
  #ifdef HTU21DF_P
    temp = htu.readTemperature() + TEMP_OFFSET;
    humidity = htu.readHumidity() + HUM_OFFSET;
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
      timestamp = timeClient.getFormattedTime();
  
      enviro = json_sample("tstamp", timestamp, LOCATION, timestamp);
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
        
  #ifdef HTU21DF_P
  
      enviro = json_sample("temp", temp, LOCATION, timestamp);
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
       
      enviro = json_sample("humidity", humidity, LOCATION, timestamp);
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
  
  #ifdef THERMISTORS
      enviro = json_sample("therm0", therms[0].tempC, LOCATION, timestamp);
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
        
      enviro = json_sample("therm1", therms[1].tempC, LOCATION, timestamp);
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

  #ifdef INA169
      enviro = json_sample("INA_169_aamps", INA169_Aamps, LOCATION, timestamp);
    #ifdef L_DEBUG_MSG
      Serial.print("Sending INA_169_aamps data: ");
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
      enviro = json_sample("gasrw", gas, LOCATION, timestamp);
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
      enviro = json_sample("gasco", gas_v_to_ppm(CARBMONO, gas), LOCATION, timestamp);
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
      enviro = json_sample("gaspr", gas_v_to_ppm(PROPANE, gas), LOCATION, timestamp);
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
     
      /*
       * send the estop pin status
       */
      enviro = json_sample("estop", cncrtr_estop, LOCATION, timestamp);
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
    } /* end of mqtt connect check */
    
    /*
     * if the mqtt connection was lost, attempt to reconnect
     */
    else {
      if(RST_ON_MQTT_FAIL)  {
        if(--mqtt_fails > 0)
          LMQTTConnect(false);
        else  {
          Serial.println("Resetting in 2 seconds because of MQTT fail");
          delay(2000);
          ESP.restart();
        }
      }
      else
        LMQTTConnect(false);
    }
#endif
/* of mqtt publish block */
    
    sampleTimerOccured = false;
  }  /* end of slow loop */

  yield();
} /* end of loop */
