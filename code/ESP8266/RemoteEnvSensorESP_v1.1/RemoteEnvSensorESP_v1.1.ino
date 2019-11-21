
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
 * 12/06/17 - 4/21/18
 * 
 * Firmware to acquire environmental parameters and send via mqtt to broker
 * 
 * Hardware:
 * Adafruit Huzzah ESP8266 module (for development; ESP-12S (AF P2491) target)
 * Adafruit HTU21D Temp/Humidity i2c module
 * Adafruit ADS1015 12-bit i2c ADC (P1083) (Host of thermistors and current sensing)
 * Adafruit MiCS5524 Gas Sensor (P3199) - no longer supported; see previous version (i2c support coming)
 * 
 * Pending:
 *
 * Now:
 * + include the topics in the thermistor structure ?
 * + add averaging for the mqtt send versus the neoPixel status fun
 * + blink the WIFI connected LED for heartbeat
 * + add current monitoring
 * + add neoPixel status indications
 * 
 * Longer term:
 * + add separate calibrations to thermistor structure
 * + add the location to the json string
 * + decide if there is a way to dynamically configure the network/hardware config
 * + add flash based config file (json) (e.g. MQTT server, timezone, etc.) ... use EEPROM lib
 * + if the NIST time sync fails, annotate the time with an asterisk
 * + implement a red "failed" light
 * 
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

/********************* Hardware Connections ********************/
//#define UNUSED   0  // built in LED on Huzzah
//#define UNUSED   2
//#define I2C_SDA  4  // defined elsewhere
//#define I2C_SCL  5  // defined elsewhere
#define WIFI_LED   12
//#define UNUSED   13
//#define UNUSED   14
//#define UNUSED   15
//#define UNUSED   16
//
// which hardware is actually connected?
#define HTU21DF_P       // Is the temp/hum module present
#define ADS1015_P       // Is the A/D present (for the gas reading)
#define THERMISTORS  2  // Are using the A/D to sense thermistors, and how many


/********************* WiFi Access Point ***********************/
// zimknives shop
//#define WLAN_SSID       "WIFIDBF86C"
//#define WLAN_PASS       "WFF7WQY771CH6ZRU"
//#define LOCATION        "cnc_router"

// home
#define WLAN_SSID       "ZEther-2G"
#define WLAN_PASS       "leonardo1519"
#define LOCATION        "Basement_Tech"

/*********************  MQTT Server Info  *********************/
#define SERVER      "192.168.1.10"
#define SERVERPORT  1883

/********************* MQTT Payload Information ***************/
#define TOPIC_TEST      "PamTest-esp8266"
#define TOPIC_ENV_TEMP  "zk-env/temp"
#define TOPIC_ENV_HUM   "zk-env/humidity"
#define TOPIC_ENV_GASCO   "zk-env/gasco"
#define TOPIC_ENV_GASPR "zk-env/gaspr"
#define TOPIC_ENV_GASRW  "zk-env/gasrw"
#define TOPIC_STIME     "zk-env/time"
#define TOPIC_ENV_THERM0    "zk-env/therm0"
#define TOPIC_ENV_THERM1    "zk-env/therm1"

/********************* Behavioral Characteristics *************/
// delay for the main sensing loop
// samples are sent this often in mS
#define MQTT_INTERVAL  5000 // Currently not used
#define ACQ_INTERVAL   250  // A/D sampling interval

// display debug messages in the loop if defined
// the messages really don't take much time, however ... maybe 10 mS
#define L_DEBUG_MSG
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
 * use these to do more repeatable loop timing
 * only delay the amount of the SEND_INTERVAL that wasn't used up by other calls
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
 * (Note that the interface circuitry uses a voltage divider to insure
 * that the sensor output stays within the limit of the ADC powered at
 * 3.3v.  Therefore each ADC bit is 2mV from the sensor.)
 */
#define ADC_GAIN         GAIN_TWO
#define ADC_SAMPLES      3  /* just for electrical noise filtering */
#define ADC_INTERVAL     5  /* mS between averaged samples */
#define ADC_AVG_SAMPLES  4  /* samples to be averaged for mqtt publish */

int adc_count;  /* number of samples acquired before average is calculated */

int i, j, k;  /* looping parameters; not intended to be persistent */

int16_t adc[4]; /* raw adc readings */
float   adcsum;  /* accumulator used for averaging */

#ifdef ADS1015_P
// Instantiate the ADC for the gas sensor
Adafruit_ADS1015 ads;
#endif

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
 *  NIST NETWORK TIME
 *  -----------------
 *  seconds to get from Greenwich to CST
 */
#define TZ_OFFSET      -21600

// Instantiate the UDP and Network Time Protocol
WiFiUDP ntpUDP;  
NTPClient timeClient(ntpUDP, TZ_OFFSET);


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
 * WIFI
 */

// Instantiate an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient WiFiclient;

/* 
 *  Connect to WiFi access point.
 *  Put it in a function in case re-connect is necessary.
 */
wl_status_t  LWifiConnect(bool first)  {
  Serial.println();
  if(first == true)
    Serial.print("Connecting to ");
  else
    Serial.print("Re-connecting to ");
  Serial.println(WLAN_SSID);

  // Wait for the connection to succeed ... wait forever
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  return(WiFi.status());
}


/*
 * MQTT
 * ----
 */

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message back from broker");
  Serial.println(topic);
}

// Setup the MQTT client class by passing in the WiFi client and MQTT server
PubSubClient mqtt(SERVER, SERVERPORT, callback, WiFiclient);

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
  
//#define NEW_STUFF
#ifdef NEW_STUFF
  // Clean up any garbage on the input line
  while(Serial.available() > 0)
    Serial.read();

  // let all of the devices stabilize after power-up; use the time to allow char entry
  Serial.println("");
  Serial.println("Press any key in the next 5 seconds to enter configuration mode");

  i = 10;
  while((Serial.available() <= 0) && (i > 0))  {
    Serial.print(i);
    Serial.print(" ... ");
    delay_yield(500);   
    i--;
  }
  Serial.println("");
  
  if(Serial.available() > 0)  {
    while(Serial.read() > 0); // read the interrupting char and any other garbage

    /*
     * Prompt and set the configuration values
     */
    Serial.println(" ... all done ... continuing  ...");
  }
#endif
//delay_yield(1000);

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
#endif

  currentMillis = previousMillis = millis();

  adc_count = 0;
  
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
   * at this writing, th eloop is taking about 190 mS
   * 
   */
  if(acqTimerOccured == true)  {
    /*
     * keep track of milliseconds used in the fast loop
     */
    previousMillis = millis();
    
    /*
     * read the physical sensors, if they exist
     */
  #ifdef HTU21DF_P
    temp = htu.readTemperature() + TEMP_OFFSET;
    humidity = htu.readHumidity() + HUM_OFFSET;
  #endif
  
  #ifdef ADS1015_P
    for (i = 0; i < 4; i++)  {   /* loop through the adc channels */
      adcsum = 0;
      for(j = 0; j < ADC_SAMPLES; j++)  {  /* make the requested number of samples in the average */
        adcsum += ads.readADC_SingleEnded(i);
        delay(ADC_INTERVAL);
      }
      adc[i] = adcsum / ADC_SAMPLES;
    #ifdef FL_DEBUG_MSG
      Serial.print("ADC["); Serial.print(i); Serial.print("] = "); Serial.println(adc[i]);
    #endif
    }
    
  #endif
  
  #ifdef THERMISTORS
    for(i = 0; i < THERMISTORS; i++)  {
    #ifdef FL_DEBUG_MSG
      Serial.print("Thermistor # "); Serial.print(i); Serial.println(" :");
    #endif
      therms[i].thermR = seriesR / ((adcMax/adc[therms[i].adc_ch]) - 1);
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
      
    
    if(adc_count < ADC_AVG_SAMPLES)
      adc_count++;
    else  {
      adc_count = 0;
    #ifdef FL_DEBUG_MSG
      Serial.println("Acq average count reached");
    #endif
    }

    /*
     * how much time used in theloop
     */
    currentMillis = millis();
  #ifdef FL_DEBUG_MSG
    Serial.print("Time used in loop: "); Serial.println(currentMillis - previousMillis);
    Serial.println();
  #endif
    acqTimerOccured = false;
  }  /* end of acquisition loop */
  
  /*
   * check for the publish/slower tick timer
   */
  if(sampleTimerOccured == true)  {


    Serial.println("Tick Occured");
  
    /*
     * Status the WIFI and set the LED indicator accordingly
     */
    if(WiFi.status() == WL_CONNECTED)
      digitalWrite(WIFI_LED, LOW);
    else {
      digitalWrite(WIFI_LED, HIGH);
      LWifiConnect(false);
    }
  
    // Service the NTP client. Updates happen much slower per defaults.
    timeClient.update();

    /*
     * Publish the data
     */
    if (mqtt.connected()) {
  
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
  
      
    }
    /*
     * if the mqtt connection was lost, attempt to reconnect
     */
    else  {
      LMQTTConnect(false);
    }
  

    
    sampleTimerOccured = false;
  }  /* end of slow loop */

  yield();
} /* end of loop */
