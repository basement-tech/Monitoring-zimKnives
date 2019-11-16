
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
 * 12/06/17 - 4/21/18
 * 
 * Firmware to acquire environmental parameters and send via mqtt to broker
 * 
 * Hardware:
 * Adafruit Huzzah ESP8266 module (for development; ESP-12S (AF P2491) target)
 * Adafruit HTU21D Temp/Humidity i2c module
 * Adafruit ADS1015 12-bit i2c ADC (P1083) (Host of Gas Sensor)
 * Adafruit MiCS5524 Gas Sensor (P3199)
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
 * Pending:

 * + add the location to the json string
 * + decide if there is a way to dynamically configure the network/hardware config
 * + add flash based config file (json) (e.g. MQTT server, timezone, etc.) ... use EEPROM lib
 * + if the NIST time sync fails, annotate the time with an asterisk
 * + implement a red "failed" light
 */

#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <Adafruit_HTU21DF.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>

/********************* Hardware Connections ********************/
//#define UNUSED   0  // built in LED on Huzzah
//#define UNUSED   2
//#define I2C_SDA  4  // defined elsewhere
//#define I2C_SCL  5  // defined elsewhere
//#define UNUSED   12
#define   WIFI_LED 13
//#define UNUSED   14
//#define UNUSED   15
//#define UNUSED   16
//
// which hardware is actually connected?
#define HTU21DF_P   // Is the temp/hum module present
#define ADS1015_P   // Is the A/D present (for the gas reading)
#define MICS5524_P  // Is the gas sensor connected to the A/D ... requires ADS1015_P, not checked


/********************* WiFi Access Point ***********************/
// zimknives shop
#define WLAN_SSID       "your ssid"  // as string
#define WLAN_PASS       "your wifi password"  // as string
#define LOCATION        "your location"  // as string

/*********************  MQTT Server Info  *********************/
#define SERVER      "your MQTT broker IP address" // as string
#define SERVERPORT  your MQTT broker port  // as integer

/********************* MQTT Payload Information ***************/
#define TOPIC_TEST      "PamTest-esp8266"
#define TOPIC_ENV_TEMP  "zk-env/temp"
#define TOPIC_ENV_HUM   "zk-env/humidity"
#define TOPIC_ENV_GASCO   "zk-env/gasco"
#define TOPIC_ENV_GASPR "zk-env/gaspr"
#define TOPIC_ENV_GASRW  "zk-env/gasrw"
#define TOPIC_STIME     "zk-env/time"

/********************* Behavioral Characteristics *************/
// delay for the main sensing loop
// samples are sent this often in mS
#define SEND_INTERVAL  5000

/* 
 * ADC gain for the gas sensor
 * 
 * With GAIN_TWO full scale is +/- 2.048 v according to the data sheet.
 * 12 bit adc(i.e. +/- 11-bits (2048)) results in a perfect 1 mV/bit.
 * (Note that the interface circuitry uses a voltage divider to insure
 * that the sensor output stays within the limit of the ADC powered at
 * 3.3v.  Therefore each ADC bit is 2mV from the sensor.)
 */
#define GAIN_GAS       GAIN_TWO

/*
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

// seconds to get from Greenwich to CST
#define TZ_OFFSET      -21600

/* Temp and Humidity calibration
 * I measured the temp at 72, 44, 27 deg F.  The sensor was
 * about the same and 1.41 deg C  too high on average.  I don't have
 * a good way to measure humidity.
 */
#define TEMP_OFFSET   -1.41
#define HUM_OFFSET     0

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message back from broker");
  Serial.println(topic);
}

/*
 * Instantiate the various connection and hardware classes
 */
// Instantiate an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient WiFiclient;

// Instantiate the UDP and Network Time Protocol
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, TZ_OFFSET);

// Setup the MQTT client class by passing in the WiFi client and MQTT server
PubSubClient mqtt(SERVER, SERVERPORT, callback, WiFiclient);

#ifdef HTU21DF_P
// Instantiate the temp/humidity sensor
Adafruit_HTU21DF htu = Adafruit_HTU21DF();
#endif

#ifdef ADS1015_P
// Instantiate the ADC for the gas sensor
Adafruit_ADS1015 ads;
#endif

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
  digitalWrite(WIFI_LED, LOW);
  if(LWifiConnect(true) == WL_CONNECTED)
    digitalWrite(WIFI_LED, HIGH);

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
  // Setup the ADC for the gas sensor acquisition
  Serial.println("Connecting to ADC");
  ads.begin();
#endif

#ifdef MICS5524_P
  // Set the gain of the gas sensor
  ads.setGain(GAIN_GAS);
#endif


}

/*
 * ***************  LOOP  ***************
 */
void loop() {
  
  /*
   * Status the WIFI and set the LED indicator accordingly
   */
  if(WiFi.status() == WL_CONNECTED)
    digitalWrite(WIFI_LED, HIGH);
  else {
    digitalWrite(WIFI_LED, LOW);
    LWifiConnect(false);
  }

  // Service the NTP client. Updates happen much slower per defaults.
  timeClient.update();

  /*
   * read the physical sensors
   */
#ifdef HTU21DF_P
  temp = htu.readTemperature() + TEMP_OFFSET;
  humidity = htu.readHumidity() + HUM_OFFSET;
#endif

#ifdef MICS5524_P
  gas = ads.readADC_Differential_0_1();
#endif
    
  /*
   * Publish the data
   */
  if (mqtt.connected()) {

    /*
     * Prepare and send the sample time ... do this first to be close to the acquition.
     */
    timestamp = timeClient.getFormattedTime();

    enviro = json_sample("tstamp", timestamp, LOCATION, timestamp);
    
    Serial.print("Sending sample time: ");
    if (mqtt.publish(TOPIC_STIME, (char*) enviro.c_str()))
      Serial.println("Publish ok");
    else
      Serial.println("Publish failed");
      
#ifdef HTU21DF_P

    enviro = json_sample("temp", temp, LOCATION, timestamp);
    
    Serial.print("Sending enviro-temp data: ");
    
    if (mqtt.publish(TOPIC_ENV_TEMP, (char*) enviro.c_str()))
      Serial.println("Publish ok");
    else
      Serial.println("Publish failed");


    /*
     * prep and send the humidity data
     */
     
    enviro = json_sample("humidity", humidity, LOCATION, timestamp);
    
    Serial.print("Sending enviro-hum data: ");
    
    if (mqtt.publish(TOPIC_ENV_HUM, (char*) enviro.c_str()))
      Serial.println("Publish ok");
    else
      Serial.println("Publish failed");
#endif

#ifdef MICS5524_P
    /*
     * send the raw gas sensor data
     */
    enviro = json_sample("gasrw", gas, LOCATION, timestamp);
    
    Serial.print("Sending enviro-gasrw data: ");
    
    if (mqtt.publish(TOPIC_ENV_GASRW, (char*) enviro.c_str()))
      Serial.println("Publish ok");
    else
      Serial.println("Publish failed");

    /*
     * send the gas sensor data converted to PPM as if CO
     */   
    enviro = json_sample("gasco", gas_v_to_ppm(CARBMONO, gas), LOCATION, timestamp);
        
    Serial.print("Sending enviro-gasco data as Carbon Monoxide PPM: ");
    
    if (mqtt.publish(TOPIC_ENV_GASCO, (char*) enviro.c_str()))
      Serial.println("Publish ok");
    else
      Serial.println("Publish failed");

    /*
     * send the gas sensor data converted to PPM as if Propane
     */
    enviro = json_sample("gaspr", gas_v_to_ppm(PROPANE, gas), LOCATION, timestamp);
    
    Serial.print("Sending enviro-gaspr data as Propane PPM: ");
    
    if (mqtt.publish(TOPIC_ENV_GASPR, (char*) enviro.c_str()))
      Serial.println("Publish ok");
    else
      Serial.println("Publish failed");
#endif

    
  }
  /*
   * if the mqtt connection was lost, attempt to reconnect
   */
  else  {
    LMQTTConnect(false);
  }

  delay(SEND_INTERVAL);
  
}
