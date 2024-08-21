
#ifdef ARDUINO_ESP8266_WEMOS_D1MINI

#include <ESP8266WiFi.h>

#include <XPT2046_Touchscreen.h>
#include "TouchControllerWS.h"
#include <MiniGrafx.h>
#include <ILI9341_SPI.h>
#include "time.h"

#include "disp_settings.h"
#include "disp_icons.h"

#include "bt_mqttlib.h"

/*
 * define some graphics elements for drawing
 */
#include "ArialRounded.h"

#define MINI_BLACK 0
#define MINI_WHITE 1
#define MINI_YELLOW 2
#define MINI_BLUE 3

#define MAX_FORECASTS 12

// defines the colors usable in the paletted 16 color frame buffer
uint16_t palette[] = {ILI9341_BLACK, // 0
                      ILI9341_WHITE, // 1
                      ILI9341_YELLOW, // 2
                      0x7E3C
                     }; //3

                     
/*
 * describe the characteristics of the physical display screen
 */
int SCREEN_WIDTH = 240;
int SCREEN_HEIGHT = 320;
// Limited to 4 colors due to memory constraints
int BITS_PER_PIXEL = 2; // 2^2 =  4 color

ILI9341_SPI tft = ILI9341_SPI(TFT_CS, TFT_DC);
MiniGrafx gfx = MiniGrafx(&tft, BITS_PER_PIXEL, palette);

XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
TouchControllerWS touchController(&ts);

void calibrationCallback(int16_t x, int16_t y);
CalibrationCallback calibration = &calibrationCallback;

/*
 * describe the macro virtual screen characteristics
 */
int screenCount = 3;
long lastDownloadUpdate = millis();

uint16_t screen = 0;  // which screen number is being displayed
long timerPress;
bool canBtnPress;

/*
 * local function call declarations
 */
static void drawProgress(uint8_t percentage, String text);
static void drawTime(time_t now);
static void drawWifiQuality();
static void drawSubscribedTopicData();
static void drawLabelValue(uint8_t line, String label, String value);
static void drawForecastTable(uint8_t start);
static void drawAbout();
static void drawSeparator(uint16_t y);

void init_touchscreen(bool cal)  {
//    loadPropertiesFromSpiffs();

  /* 
   * The LED pin needs to set HIGH
   * Use this pin to save energy
   * Turn on the background LED
   */
  Serial.println(TFT_LED);
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);    // HIGH to Turn on;

  /*
   * initialize minigfx
   */
  gfx.init();
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();

  /*
   * initialize the touch screen device
   * (separate from the display part of the screen)
   */
  Serial.println("Initializing touch screen...");
  ts.begin();
  TS_Point p = ts.getPoint();  // throw away the startup point - added DJZ
 
  /*
   * mount and optionally reformat the SPIFFS,
   * which erases the screen calibration and starts it again
   */
  Serial.println("Mounting file system...");
  bool isFSMounted = SPIFFS.begin();

  /*
   * if the file system mount failed, assume that it
   * needs to be formatted
   */
  if (!isFSMounted ) {
    Serial.println("Formatting file system...");
    drawProgress(50, "Formatting file system");
    SPIFFS.format();
    drawProgress(100, "Formatting done");
  }

  /*
   * display information about the filesystem
   */
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  Serial.print("fs_info.totalBytes = "); Serial.println(fs_info.totalBytes);
  Serial.print("fs_info.usedBytes = "); Serial.println(fs_info.usedBytes);
  Serial.print("fs_info.blockSize = "); Serial.println(fs_info.blockSize);
  Serial.print("fs_info.pageSize = "); Serial.println(fs_info.pageSize);
  Serial.print("fs_info.maxOpenFiles = "); Serial.println(fs_info.maxOpenFiles);
  Serial.print("fs_info.maxPathLength = "); Serial.println(fs_info.maxPathLength);
  
  boolean isCalibrationAvailable = touchController.loadCalibration();
  if (!isCalibrationAvailable || (cal == true)) {
    Serial.println("Calibration not available");
    touchController.startCalibration(&calibration);
    while (!touchController.isCalibrationFinished()) {
      gfx.fillBuffer(0);
      gfx.setColor(MINI_YELLOW);
      gfx.setTextAlignment(TEXT_ALIGN_CENTER);
      gfx.drawString(120, 160, "Please calibrate\ntouch screen by\ntouch point");
      touchController.continueCalibration();
      gfx.commit();
      yield();
    }
    touchController.saveCalibration();
  }
}

TS_Point last_point_touched;
bool point_touched = false;
void touchWasTouched(void)  {
    if (touchController.isTouched(0)) {
      point_touched = true;
      last_point_touched = touchController.getPoint();
    }
}

/*
 * update the touchscreen display
 */
void disp_update(time_t now)  {
  
  /*
   * if the screen has been touched, take the indicated action
   */
  if(point_touched == true)  {
    point_touched = false;

    Serial.printf("Touch point detected at %d/%d.\n", last_point_touched.x, last_point_touched.y);

    /*
     * if the top of the screen has been touched,
     * change the time format
     */
    if (last_point_touched.y < 80) {
      IS_STYLE_12HR = !IS_STYLE_12HR;
    }

    /*
     * else increment to the next screen
     */
    else {
      screen++;
      if(screen >= screenCount)
        screen = 0;
    }
  }
  
  /*
   * clear the screen at the start of each update cycle
   */
  gfx.fillBuffer(MINI_BLACK);
  
  if (screen == 0) {
    drawTime(now);
    drawWifiQuality();
  } 
  else if (screen == 1) {
    drawAbout();
  }
  else if (screen == 2)
    drawSubscribedTopicData();
    
  gfx.commit();
}


// Progress bar helper
void drawProgress(uint8_t percentage, String text) {
  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(20, 5, ThingPulseLogo);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 90, "https://thingpulse.com");
  gfx.setColor(MINI_YELLOW);

  gfx.drawString(120, 146, text);
  gfx.setColor(MINI_WHITE);
  gfx.drawRect(10, 168, 240 - 20, 15);
  gfx.setColor(MINI_BLUE);
  gfx.fillRect(12, 170, 216 * percentage / 100, 11);

  gfx.commit();
}

// draws the clock
void drawTime(time_t now) {
  char time_str[11];
//  time_t now = time(nullptr);
  struct tm * timeinfo = localtime(&now);

  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setColor(MINI_WHITE);
  String date = WDAY_NAMES[timeinfo->tm_wday] + " " + MONTH_NAMES[timeinfo->tm_mon] + " " + String(timeinfo->tm_mday) + " " + String(1900 + timeinfo->tm_year);
  gfx.drawString(120, 6, date);

  gfx.setFont(ArialRoundedMTBold_36);

  if (IS_STYLE_12HR) {                                                              //12:00
    int hour = (timeinfo->tm_hour + 11) % 12 + 1; // take care of noon and midnight
    if (IS_STYLE_HHMM) {
      sprintf(time_str, "%2d:%02d\n", hour, timeinfo->tm_min);                //hh:mm
    } else {
      sprintf(time_str, "%2d:%02d:%02d\n", hour, timeinfo->tm_min, timeinfo->tm_sec); //hh:mm:ss
    }
  } else {                                                                            //24:00
    if (IS_STYLE_HHMM) {
        sprintf(time_str, "%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min); //hh:mm
    } else {
        sprintf(time_str, "%02d:%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec); //hh:mm:ss
    }
  }

  gfx.drawString(120, 20, time_str);

  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(ArialMT_Plain_10);
  gfx.setColor(MINI_BLUE);
  if (IS_STYLE_12HR) {
    sprintf(time_str, "\n%s", timeinfo->tm_hour >= 12 ? "PM" : "AM");
    gfx.drawString(195, 27, time_str);
  }
}



void drawSubscribedTopicData() {
  gfx.setFont(ArialRoundedMTBold_36);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 2, "Current");
  gfx.drawString(120, 36, "Conditions");
  gfx.drawString(120, 72, "----------");

  int i = 0;
  int line = 5;

  gfx.setFont(ArialRoundedMTBold_14);

  while(parameters[i].parm_type != PARM_UND)  {
    if(parameters[i].valid == true)  {
      Serial.print("value = <");Serial.print(parameters[i].value); Serial.println(">");
      drawLabelValue(line, parameters[i].label, String(parameters[i].value));
    }
    else
      drawLabelValue(line, parameters[i].label, String("not set"));
    i++;
    line++;
  }
/*
  // String weatherIcon;
  // String weatherText;
  drawLabelValue(0, "Temperature:", String(currentWeather.temp, 1) + (IS_METRIC ? "°C" : "°F"));
  drawLabelValue(1, "Wind Speed:", String(currentWeather.windSpeed, 1) + (IS_METRIC ? "m/s" : "mph") );
  drawLabelValue(2, "Wind Dir:", String(currentWeather.windDeg, 1) + "°");
  drawLabelValue(3, "Humidity:", String(currentWeather.humidity) + "%");
  drawLabelValue(4, "Pressure:", String(currentWeather.pressure) + "hPa");
  drawLabelValue(5, "Clouds:", String(currentWeather.clouds) + "%");
  drawLabelValue(6, "Visibility:", String(currentWeather.visibility) + "m");
*/
}

void drawLabelValue(uint8_t line, String label, String value) {
  const uint8_t labelX = 15;
  const uint8_t valueX = 150;
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(labelX, 30 + line * 15, label);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(valueX, 30 + line * 15, value);
}

// converts the dBm to a range between 0 and 100%
int8_t getWifiQuality() {
  int32_t dbm = WiFi.RSSI();
  if (dbm <= -100) {
    return 0;
  } else if (dbm >= -50) {
    return 100;
  } else {
    return 2 * (dbm + 100);
  }
}

void drawWifiQuality() {
  int8_t quality = getWifiQuality();
  gfx.setColor(MINI_WHITE);
  gfx.setFont(ArialMT_Plain_10);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.drawString(228, 9, String(quality) + "%");
  for (int8_t i = 0; i < 4; i++) {
    for (int8_t j = 0; j < 2 * (i + 1); j++) {
      if (quality > i * 25 || j == 0) {
        gfx.setPixel(230 + 2 * i, 18 - j);
      }
    }
  }
}


void drawAbout() {
  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(20, 5, ThingPulseLogo);

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setColor(MINI_WHITE);
  gfx.drawString(120, 90, "https://thingpulse.com");

  gfx.setFont(ArialRoundedMTBold_14);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  drawLabelValue(7, "Heap Mem:", String(ESP.getFreeHeap() / 1024) + "kb");
  drawLabelValue(8, "Flash Mem:", String(ESP.getFlashChipRealSize() / 1024 / 1024) + "MB");
  drawLabelValue(9, "WiFi Strength:", String(WiFi.RSSI()) + "dB");
  drawLabelValue(10, "Chip ID:", String(ESP.getChipId()));
  drawLabelValue(11, "VCC: ", String(ESP.getVcc() / 1024.0) + "V");
  drawLabelValue(12, "CPU Freq.: ", String(ESP.getCpuFreqMHz()) + "MHz");
  char time_str[15];
  const uint32_t millis_in_day = 1000 * 60 * 60 * 24;
  const uint32_t millis_in_hour = 1000 * 60 * 60;
  const uint32_t millis_in_minute = 1000 * 60;
  uint8_t days = millis() / (millis_in_day);
  uint8_t hours = (millis() - (days * millis_in_day)) / millis_in_hour;
  uint8_t minutes = (millis() - (days * millis_in_day) - (hours * millis_in_hour)) / millis_in_minute;
  sprintf(time_str, "%2dd%2dh%2dm", days, hours, minutes);
  drawLabelValue(13, "Uptime: ", time_str);
  drawLabelValue(14, "IP Address: ", WiFi.localIP().toString());
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(15, 280, "Last Reset: ");
  gfx.setColor(MINI_WHITE);
  gfx.drawStringMaxWidth(15, 295, 240 - 2 * 15, ESP.getResetInfo());
}

void calibrationCallback(int16_t x, int16_t y) {
  gfx.setColor(1);
  gfx.fillCircle(x, y, 10);
}

String getTime(time_t *timestamp) {
  struct tm *timeInfo = localtime(timestamp);

  char buf[6];
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  return String(buf);
}

#endif //ARDUINO_ESP8266_WEMOS_D1MINI
