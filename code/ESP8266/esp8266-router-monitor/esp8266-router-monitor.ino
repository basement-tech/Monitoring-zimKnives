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
 * CNCRouterMonitorESP
 * 10/21/19 -
 * 
 * Firmware to monitor state of CNC router, including temperatures, estop, and
 * current draw of motors.
 * 
 * Hardware:
 * Adafruit Huzzah ESP8266 module (P2491)

 * v0.1:
 * + monitor estop signal and indicate state via NeoPixel strip
 *   mounted in CNC router control box
 * 
 * Pending:
 * - current sense
 * - read temperatures of heat sinks and ambient case
 * - send telemetry via mqtt to broker
 * 
 * Notes:
 * From Adafruit re: NeoPixel wiring
 * - Add 1000 uF CAPACITOR between NeoPixel strip's + and - connections.
 * - NeoPixel strip's DATA-IN should pass through a 300-500 OHM RESISTOR.
 * - When using a 3.3V microcontroller with a 5V-powered NeoPixel strip,
 *   a LOGIC-LEVEL CONVERTER on the data line is STRONGLY RECOMMENDED.
 * DO NOT USE PIN 16 FOR NEOPIXELS! WHY? JUST DON'T!
 */
 
#include <Adafruit_NeoPixel.h>

// NeoPixel definitions
#define LED_PIN 15
#define LED_COUNT 30

// estop signal definitions
#define ESTOP_PIN 13
#define ESTOP_PRESSED HIGH

// status LED definitions
#define STATUS_PIN 12

// define NeoPixel object
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// initiliaze estop state
int estopState = 0;

// initiliaze status LED state
int statusState = 0;

void setup() {
  // NeoPixel setup
  strip.begin(); // initialize NeoPixels
  strip.show(); // turn off all pixels
  strip.setBrightness(127); // out of 255

  // estop signal setup
  pinMode(ESTOP_PIN, INPUT);

  // status LED setup
  pinMode(STATUS_PIN, OUTPUT);
}
 
void loop() {
  // check estop status and update LEDs
  if (machineIsEstopped()) {
    flashRedLeds();
  } else {
    steadyGreenLeds();
  }

  // blink status LED
  if (statusState==0) {
    digitalWrite(STATUS_PIN, HIGH);
    statusState = 1;
  } else {
    digitalWrite(STATUS_PIN, LOW);
    statusState = 0;
  }
}

// To eliminate any confusion about estop signaling, this function
// returns true if the machine is estopped, and false otherwise.
boolean machineIsEstopped () {
  estopState = digitalRead(ESTOP_PIN);
  return (estopState == ESTOP_PRESSED);
}

// Steady solid green with 1 second cycle
void steadyGreenLeds() {
  uint32_t green = strip.Color(0, 255, 0);
  setSolidColor(green);
  delay(1000);
}

// Flash solid red with 1 second cycle
void flashRedLeds() {
  uint32_t red = strip.Color(255, 0, 0);
  uint32_t blank = strip.Color(0, 0, 0);
  setSolidColor(red);
  delay(500);
  setSolidColor(blank);
  delay(500);
}

// Set the whole strip of NeoPixels to a single color
void setSolidColor(uint32_t color) {
  for(int i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}
