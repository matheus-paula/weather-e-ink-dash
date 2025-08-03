/**
 * The MIT License (MIT)
 *
 * This code is written and maintained by Matheus de Paula (https://github.com/matheus-paula)
 * 
 *  Copyright (c) 2025 Matheus de Paula (https://github.com/matheus-paula)
 *
 *  It utilizes code from the following libraries as well:
 *  - QR-Code generator by (Richard Moore - https://github.com/ricmoo/QRCode | Project Nayuki - https://www.nayuki.io/page/qr-code-generator-library)
 *  - GxEPD2 - E-paper library by (ZinggJM - https://github.com/ZinggJM/GxEPD2/tree/master)
 *  - Adafruit GFX library FreeSans and FreeSerif fonts (https://github.com/adafruit/Adafruit-GFX-Library/tree/master/Fonts)
 *  - Wifi Manager by (Tzapu - https://github.com/tzapu/WiFiManager)
 *  - ESP32 Arduino core (Preferences.h, HTTPClient.h, time.h - https://github.com/espressif/arduino-esp32)
 *  - Arduino Json by (Benoît Blanchon - https://github.com/bblanchon/ArduinoJson)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 **/

// Icons and auth/config files
#include <icons.cpp>

// Epaper library and fonts
#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>

// Wifi, Json and Neopixel libs
#include <Preferences.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "qrcode.h"

//#define USE_MOCK_API //uncomment to use a temporary mock api to avoid hit usage limit during testing
#define OW_MOCK_API "api-mock.json" // mock api
#define OW_BASE_API_URL "https://api.openweathermap.org/data/2.5/forecast"

// Deep Sleep configuration
#define RUNS_PER_DAY 144
#define SECONDS_IN_DAY 86400
#define SLEEP_INTERVAL_SEC (SECONDS_IN_DAY / RUNS_PER_DAY)
#define RESET_PIN 21
#define BATTERY_LEVEL_PIN 0

// Used to manage deep sleep state
RTC_DATA_ATTR int wakeCount = 0;
RTC_DATA_ATTR bool triggerReset = false;

/** AP network and password used to configure the network at first boot */
const char* AP_SSID = "ESP32-WeatherDash";
const char* AP_PASSWORD = "connectweatherdash";

// Status Led GPIO
#define STATUS_LED 15

// ESP-32-c6 Super Mini  board
// Display Serial interface
#define SCK 4
#define MISO -1
#define MOSI 6
#define CS 7
#define DC 1
#define RES 2
#define BUSY 3

// Display Serial interface pins example for ESP32-C3 CS(SS)=7, SCL(SCK)=4, SDA(MOSI)=6, BUSY=3, RES(RST)=2, DC=1
// GDEM029C90 128x296, SSD1680
// This code is epecifically design to work on a 182x296px epaper module from WeAct Studio
GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(GxEPD2_290_C90c(CS, DC, RES, BUSY)); 

bool wasConnectedBefore = false;

/** Preferences */
Preferences preferences;
String owUnits = "metric";
String owLang = "pt_br";
String owLat = "-14.2400732";
String owLon = "-53.1805017";
String owHourMode = "24";
String tmz = "-03:00";
String owToken = "";
String haToken = "";
String haAddress = "";

/**Home Assistant Sensors Identifiers*/
String sensor1_h = "";
String sensor1_temp = "";
String sensor2_h = "";
String sensor2_temp = "";
String sensor3_h = "";
String sensor3_temp = "";

String resetMessageTemplate = 
  "{{TITLE}}\n\n"
  "Please, connect again on:\n\n"
  "SSID: {{SSID}}\n\n"
  "Password: {{PASSWORD}}\n\n"
  "IP: 192.168.4.1\n\n"
  "You can also point your phone\n"
  "camera to the QR code aside\n"
  "and connect automatically!";

// Web server used to configure first use
WebServer server(80);
bool firstBoot = false;

// Return a templated message of reset status
String getResetMessage(const String& title, const String& ssid, const String& password) {
  String msg = resetMessageTemplate;
  msg.replace("{{TITLE}}", title);
  msg.replace("{{SSID}}", ssid);
  msg.replace("{{PASSWORD}}", password);
  return msg;
}

// Remove from the text diacritics, since the embedded font does not support all of them
String removeDiacritics(const String& input) {
  String output = "";
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if ((uint8_t)c == 0xC3 && i + 1 < input.length()) {
      char next = input[i + 1];
      switch ((uint8_t)next) {
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
          output += 'a'; break;
        case 0xA8: case 0xA9:
          output += 'e'; break;
        case 0xAC: case 0xAD:
          output += 'i'; break;
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
          output += 'o'; break;
        case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD:
          output += 'u'; break;
        case 0xA7:
          output += 'c'; break;
        case 0xB1:
          output += 'n'; break;
        case 0x80:
        case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
          output += 'A'; break;
        case 0x88: case 0x89:
          output += 'E'; break;
        case 0x8C: case 0x8D:
          output += 'I'; break;
        case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
          output += 'O'; break;
        case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D:
          output += 'U'; break;
        case 0x87:
          output += 'C'; break;
        case 0x91:
          output += 'N'; break;
        default:
          output += '?'; break;
      }
      i++;
    } else {
      output += c;
    }
  }
  return output;
}

// Get Timezone String information to use on the format function
String getTZString() {
  const char* gmt = tmz.c_str();
  if (tmz.length() < 5) return "UTC0"; // Safety check for malformed input

  char sign = gmt[0];
  int hour = (gmt[1] - '0') * 10 + (gmt[2] - '0');
  int minute = (gmt[3] - '0') * 10 + (gmt[4] - '0');

  int totalOffset = hour * 60 + minute;
  int offset = (sign == '-') ? -totalOffset : totalOffset;

  int h = abs(offset) / 60;
  int m = abs(offset) % 60;

  char tz[16];
  if (m == 0)
    sprintf(tz, "UTC%+d", offset / 60);
  else
    sprintf(tz, "UTC%+d:%02d", offset / 60, m);

  return String(tz);
}

// Format the given unix timestamp data accordingly
String formatUnixTimestamp(time_t timestamp) {
  String tz = getTZString();
  setenv("TZ", tz.c_str(), 1);
  tzset();
  struct tm *timeinfo = localtime(&timestamp);
  const char* hourFormat = owHourMode.equals("12") ? "%I:%M %p" : "%H:%M";
  const char* dateFormat = owUnits.equals("imperial") ? "%m/%d/%Y" : "%d/%m/%Y";
  char formatStr[40];
  snprintf(formatStr, sizeof(formatStr), "%s - %s", hourFormat, dateFormat);
  char buffer[40];
  strftime(buffer, sizeof(buffer), formatStr, timeinfo);
  return String(buffer);
}

// Display a full screen message with a option to show a QR code aside
void showFullScrMessage(String message, String qrCodeStr) {
  display.setRotation(1);
  display.setFont();
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 10);
    display.print(message);
    if (qrCodeStr.length() > 0) {
      int qrX = display.width() - 90;
      int qrY = display.height() - 100;
      drawQRCode(qrCodeStr, qrX, qrY, 3);
    }
  } while (display.nextPage());
  display.hibernate();
}

// Fetch Home Assistant sensors data
String fetchSensorData(String sensor, bool showUnit) {
  HTTPClient http;
  String url = haAddress + "/api/states/" + sensor;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + haToken);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON deserialization failed: ");
      Serial.println(error.c_str());
      http.end();
      return "N/A";
    }

    if (!doc.containsKey("state")) {
      Serial.println("Missing 'state' in JSON");
      http.end();
      return "N/A";
    }
    http.end();
    
    if(doc["state"] == "unavailable"){
      return "N/A";
    }

    return doc["state"] | "N/A";

  } else {
    Serial.println("Error on HTTP request: " + String(httpCode));
    http.end();
    return "N/A";
  }
}

//Get the icon that match the API code
const unsigned char* getIcon(String icon) {
  if (icon == "01d") return ui_clearsky_day;
  else if (icon == "02d") return ui_partlycloudy_day;
  else if (icon == "03d" || icon == "04d") return ui_cloudy;
  else if (icon == "09d") return ui_rainshowers_day;
  else if (icon == "10d") return ui_rain;
  else if (icon == "11d") return ui_heavyrain;
  else if (icon == "13d") return ui_snow;
  else if (icon == "50d") return ui_fog;
  else if (icon == "01n") return ui_clearsky_night;
  else if (icon == "02n") return ui_partlycloudy_night;
  else if (icon == "03n" || icon == "04n") return ui_cloudy;
  else if (icon == "09n") return ui_rainshowers_night;
  else if (icon == "10n") return ui_rain;
  else if (icon == "11n") return ui_heavyrainshowersandthunder_night;
  else if (icon == "13n") return ui_snow;
  else if (icon == "50n") return ui_fog;
  else return ui_partlycloudy_day; // default
}

// Draw the actual QR code on the display
void drawQRCode(String text, int x, int y, int size) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, text.c_str());
  for (uint8_t iy = 0; iy < qrcode.size; iy++) {
    for (uint8_t ix = 0; ix < qrcode.size; ix++) {
      if (qrcode_getModule(&qrcode, ix, iy)) {
        display.fillRect(x + ix * size, y + iy * size, size, size, GxEPD_BLACK);
      } else {
        display.fillRect(x + ix * size, y + iy * size, size, size, GxEPD_WHITE);
      }
    }
  }
}

// This code convert the expected temperature from the sensor in celsius and convert it for match the set preferences the user set earlier
String convertTemperature(String celsius) {
  float value = celsius.toFloat();
  if(isnan(value)){
    return "N/A";
  }
  if (owUnits == "imperial") {
    float fahrenheit = (value * 9.0 / 5.0) + 32;
    return String(fahrenheit, 1);
  } else if (owUnits == "standard") {
    float kelvin = value + 273.15;
    return String(kelvin, 1);
  } else {
    return String(value, 1);
  }
}

// Display on the screen a especific Home Assistant sensor temperature and humidity data
void displaySensorData(const unsigned char* icon, int x, int y, String temp, String humidity){
  int separator_x = 31 + x;
  int separator_y = y - 6;
  int temp_x = 37 + x;
  int temp_y = y + 5;
  int humid_x = 37 + x;
  int humid_y = y + 26;
  int temp_unit_x = temp_x + 36;
  int temp_unit_y = temp_y - 13;
  int percent_x = humid_x + 38;
  int percent_y = humid_y - 12;
  
  display.drawBitmap(x, y, icon, 24, 24, GxEPD_RED);
  display.fillRect(separator_x, separator_y, 2, 36, GxEPD_BLACK);
  if(!temp.equals("N/A")){
    if (owUnits.equals("imperial")) {
      display.drawBitmap(temp_unit_x, temp_unit_y, ui_farenheit_icon, 16, 16, GxEPD_BLACK);
    }else{
      if (owUnits.equals("metric")) {
        display.drawBitmap(temp_unit_x, temp_unit_y, ui_celsius_icon, 16, 16, GxEPD_BLACK);
      }else{
        display.setCursor(temp_unit_x + 8, temp_unit_y + 13);
        display.setTextColor(GxEPD_BLACK);
        display.print("K");
      }
    }
  }
  if(!humidity.equals("N/A")){
    display.drawBitmap(percent_x, percent_y, ui_percent_icon, 16, 16, GxEPD_BLACK);
  }
  display.setCursor(temp_x, temp_y);
  display.print(convertTemperature(temp));
  display.setCursor(humid_x, humid_y);
  display.print(humidity);
}

DynamicJsonDocument fetchOpenWeatherData() {
  HTTPClient http;
  String ow_api = String(OW_BASE_API_URL) + 
  "?lat=" + owLat + 
  "&lon=" + owLon + 
  "&units=" + owUnits + 
  "&lang=" + owLang + 
  "&appid=" + owToken;

  #ifdef USE_MOCK_API
    http.begin(String(OW_MOCK_API));
  #else
    http.begin(ow_api);
  #endif

  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET(); 
  String payload = "";

  if (httpCode > 0) {
    payload = http.getString();
  } else {
    Serial.println("Error on HTTP request: " + String(httpCode));
    payload = "{}";
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print("Failed to parse weather data: ");
    Serial.println(error.c_str());
    doc.clear();
  }

  http.end();
  return doc;
}

// Draw the icon accordingly to the percent level
void drawBatteryIcon(float v, int x, int y){
  const unsigned char* ui_battery_icon_levels[11] = {
    ui_icon_battery_0, ui_icon_battery_10,
    ui_icon_battery_20, ui_icon_battery_30,
    ui_icon_battery_40, ui_icon_battery_50,
    ui_icon_battery_60, ui_icon_battery_70,
    ui_icon_battery_80, ui_icon_battery_90,
    ui_icon_battery_100
  };
  if(v >= 95){
    display.drawBitmap(x, y, ui_battery_icon_levels[10], 16, 16, GxEPD_BLACK);
  }else if(v > 90){
    display.drawBitmap(x, y, ui_battery_icon_levels[9], 16, 16, GxEPD_BLACK);
  }else if(v >= 80){
    display.drawBitmap(x, y, ui_battery_icon_levels[8], 16, 16, GxEPD_BLACK);
  }else if(v >= 70){
    display.drawBitmap(x, y, ui_battery_icon_levels[7], 16, 16, GxEPD_BLACK);
  }else if(v >= 60){
    display.drawBitmap(x, y, ui_battery_icon_levels[6], 16, 16, GxEPD_BLACK);
  }else if(v >= 50){
    display.drawBitmap(x, y, ui_battery_icon_levels[5], 16, 16, GxEPD_BLACK);
  }else if(v >= 40){
    display.drawBitmap(x, y, ui_battery_icon_levels[4], 16, 16, GxEPD_BLACK);
  }else if(v >= 30){
    display.drawBitmap(x, y, ui_battery_icon_levels[3], 16, 16, GxEPD_BLACK);
  }else if(v >= 20){
    display.drawBitmap(x, y, ui_battery_icon_levels[2], 16, 16, GxEPD_BLACK);
  }else if(v >= 10){
    display.drawBitmap(x, y, ui_battery_icon_levels[1], 16, 16, GxEPD_RED);
  }else{
    display.drawBitmap(x, y, ui_battery_icon_levels[0], 16, 16, GxEPD_RED);
  }
}

float estimateBattPerc(float voltage) {
  if (voltage >= 4.2f) return 100.0f;
  if (voltage <= 3.0f) return 0.0f;

  float x = (voltage - 3.0f) / 1.2f;

  // More accurate fit (based on real-world 18650 data)
  float perc = -3.5f * x * x * x * x
               + 14.0f * x * x * x
               - 20.8f * x * x
               + 110.0f * x;

  if (perc > 100.0f) return 100.0f;
  if (perc < 0.0f) return 0.0f;
  return perc;
}

// Render the entire UI on the screen
void renderInfo(){
  // Get Open Weather Map data
  DynamicJsonDocument doc = fetchOpenWeatherData();
  float temp = doc["list"][0]["main"]["temp"] | NAN;
  float temp_max = doc["list"][0]["main"]["temp_max"] | NAN;
  float temp_min = doc["list"][0]["main"]["temp_min"] | NAN;
  String weather_status_desc = doc["list"][0]["weather"][0]["description"] | "N/A";
  String weather_status_ico = doc["list"][0]["weather"][0]["icon"] | "N/A";
  String weather_status_temp = isnan(temp) ? "N/A" : String(temp, 1);
  String weather_status_temp_max = isnan(temp_max) ? "N/A" : String(temp_max, 1);
  String weather_status_temp_min = isnan(temp_min) ? "N/A" : String(temp_min, 1);
  String weather_status_pressure = doc["list"][0]["main"]["pressure"] | "N/A";
  String weather_status_humidity = doc["list"][0]["main"]["humidity"] | "N/A";
  
  // Handle timestamp
  time_t weather_status_timestamp = doc["list"][0]["dt"];
  String timestamp = formatUnixTimestamp(weather_status_timestamp);

  // Handle wind info
  float weather_status_headingdeg = doc["list"][0]["wind"]["deg"];
  String weather_status_windspd = doc["list"][0]["wind"]["speed"];
  int angle = ((int)round(weather_status_headingdeg)) % 360;
  int heading_index = ((angle + 22) / 45) % 8;
  const unsigned char* directionIcons[8] = {
    ui_icon_N, ui_icon_NE, ui_icon_E, ui_icon_SE,
    ui_icon_S, ui_icon_SW, ui_icon_W, ui_icon_NW
  };

  // Get sensors data
  // Sensor 1
  String temp_sensor_ext = fetchSensorData(sensor1_temp, false);  
  String humidity_sensor_ext = fetchSensorData(sensor1_h, false);  
  
  // Sensor 2
  String temp_sensor_livingroom = fetchSensorData(sensor2_temp, false);
  String humidity_sensor_livingroom = fetchSensorData(sensor2_h, false);
  
  // Sensor 3
  String temp_sensor_bedroom = fetchSensorData(sensor3_temp, false);
  String humidity_sensor_bedroom = fetchSensorData(sensor3_h, false);

  //Battery level
  //Should be calibrated to match the acurracy of your ESP's ADC
  //Just divide the true measured value by the value of the 
  //calculated percentage and set here
  float correctionFactor = 1.234;
  int batt_raw = analogRead(BATTERY_LEVEL_PIN);
  float voltage = (batt_raw / 4095.0) * 3.3 * 2.0 * correctionFactor;
  float batt_perc =  estimateBattPerc(voltage);
  
  // Prepare display for rendering
  display.setRotation(1);
  display.setFullWindow();
  display.firstPage();
  display.setTextColor(GxEPD_BLACK);
  
  //Renders each part of the ui on the screen
  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw battery icon
    drawBatteryIcon(batt_perc, 216, 42);
  
    Serial.println(voltage);
    Serial.println(batt_raw);
    Serial.println(batt_perc);
    
    // Draw battery level
    display.setFont(&FreeSans9pt7b);
    display.setCursor(232, 56);
    display.print(String(batt_perc, 0) + " %");
    //display.print(String(voltage, 2) + " v");

    // Wind Information
    display.drawBitmap(24, 66, directionIcons[heading_index], 16, 16, GxEPD_RED);
    display.drawBitmap(5, 66, ui_wind_icon, 16, 16, GxEPD_BLACK);
    display.setFont(&FreeSerifItalic9pt7b);
    display.setCursor(40, 78);
    if (owUnits.equals("imperial")) {
      display.print(weather_status_windspd + " mi/s");
    }else{
      display.print(weather_status_windspd + " m/s");
    }

    // Timestamp
    display.drawBitmap(122, 64, ui_clock_icon, 16, 16, GxEPD_BLACK);
    display.setCursor(140, 78);
    display.print(timestamp);

    // Main temperature
    display.drawBitmap(5, 5, getIcon(weather_status_ico), 50, 50, GxEPD_RED);
    display.setFont(&FreeSans18pt7b);
    display.setCursor(55, 30);
    display.print(weather_status_temp);
    if(!weather_status_temp.equals("N/A")){
      if (owUnits.equals("imperial")) {
        display.drawBitmap(128, 9, ui_farenheit_icon_lg, 24, 24, GxEPD_BLACK);
      }else{
        if (owUnits.equals("metric")) {
          display.drawBitmap(128, 9, ui_celsius_icon_lg, 24, 24, GxEPD_BLACK);
        }else{
          display.setCursor(142, 31);
          display.setTextColor(GxEPD_BLACK);
          display.print("K");
        }
      }
    }
    display.setFont(&FreeSerifItalic9pt7b);
    display.setCursor(58, 52);
    if (weather_status_desc.length() > 0) {
      weather_status_desc[0] = toupper(weather_status_desc[0]);
    }
    display.print(removeDiacritics(weather_status_desc));
    
    // Min and Max temperature information
    display.setFont(&FreeSans9pt7b);
    display.drawBitmap(216, 4, ui_up_arrow, 16, 16, GxEPD_RED);
    display.setCursor(232, 17);
    display.setTextColor(GxEPD_RED);
    display.print(weather_status_temp_max);
    display.drawBitmap(216, 20, ui_down_arrow, 16, 16, GxEPD_BLACK);
    display.setCursor(232, 33);
    display.setTextColor(GxEPD_BLACK);
    display.print(weather_status_temp_min);
    
    if(!weather_status_temp_max.equals("N/A") && !weather_status_temp_min.equals("N/A")){
      if (owUnits.equals("imperial")) {
        display.drawBitmap(272, 4, ui_farenheit_icon, 16, 16, GxEPD_RED);
        display.drawBitmap(272, 21, ui_farenheit_icon, 16, 16, GxEPD_BLACK);
      }else{
        if (owUnits.equals("metric")) {
          display.drawBitmap(272, 4, ui_celsius_icon, 16, 16, GxEPD_RED);
          display.drawBitmap(272, 21, ui_celsius_icon, 16, 16, GxEPD_BLACK);
        }else{
          display.setCursor(280, 16);
          display.setTextColor(GxEPD_RED);
          display.print("K");
          display.setCursor(280, 33);
          display.setTextColor(GxEPD_BLACK);
          display.print("K");
        }
      }
    }

    // Render each Home Assistant sensor information on the screen with given coordinates
    displaySensorData(ui_home_icon, 4, 96, temp_sensor_ext, humidity_sensor_ext);
    displaySensorData(ui_bedroom_icon, 106, 96, temp_sensor_bedroom, humidity_sensor_bedroom);
    displaySensorData(ui_livingroom_icon, 202, 96, temp_sensor_livingroom, humidity_sensor_livingroom);
    
  } while (display.nextPage());
  Serial.println("All screen information rendered");
}

void loadPreferences() {
  owLang = preferences.getString("lang", "en");
  owUnits = preferences.getString("units", "metric");
  owLon = preferences.getString("lon", "-14.2400732");
  owLat = preferences.getString("lat", "-53.1805017");
  owToken = preferences.getString("ow_token", "");
  haToken = preferences.getString("ha_token", "");
  haAddress = preferences.getString("ha_address", "");
  tmz = preferences.getString("tmz", "-03:00");
  sensor1_temp = preferences.getString("sensor1_temp", "");
  sensor1_h = preferences.getString("sensor1_h", "");
  sensor2_temp = preferences.getString("sensor2_temp", "");
  sensor2_h = preferences.getString("sensor2_h", "");
  sensor3_temp = preferences.getString("sensor3_temp", "");
  sensor3_h = preferences.getString("sensor3_h", "");
  preferences.end();
}
 
void startConfigServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", R"rawliteral(<body><style>@import url(https://fonts.googleapis.com/css?family=Montserrat);*{margin:0;padding:0}html{height:auto}body{font-family:montserrat,arial,verdana;height:auto;width:100%;background:linear-gradient(rgba(196,102,0,.6),rgba(155,89,182,.6))}hr{margin:10px 0}#prf{width:90vw;height:fit-content;max-width:1200px;margin:50px auto;text-align:center;position:relative}#prf fieldset{background:#fff;border:0;border-radius:.35rem;box-shadow:0 0 15px 1px rgba(0,0,0,.4);padding:20px 30px;box-sizing:border-box;width:80%;margin:0 10%;position:relative}#prf fieldset>div{display:flex;margin:10px 0}#prf fieldset>div>input,#prf fieldset>div>select,#prf fieldset>div>textarea{margin:auto}#prf fieldset>div>label{margin:auto 5px;width:300px}#prf input,#prf select,#prf textarea{padding:15px;border:1px solid #ccc;border-radius:.35rem;margin-bottom:10px;width:100%;box-sizing:border-box;font-family:montserrat;color:#2c3e50;font-size:13px}#prf .action-button{width:100px;background:#27ae60;font-weight:700;color:#fff;border:0;border-radius:1px;cursor:pointer;padding:10px;margin:10px auto;text-decoration:none;font-size:14px}#prf .action-button:focus,#prf .action-button:hover{box-shadow:0 0 0 2px #fff,0 0 0 3px #27ae60}h1{font-size:15px;text-transform:uppercase;color:#2c3e50;margin-bottom:10px}h2{font-weight:400;font-size:13px;color:#666;margin-bottom:20px}</style><form action=/save id=prf><fieldset><h1>Configure OpenWeatherMap</h1><hr><div><label>OpenWeatherMap Token:</label> <input name=ow_token></div><div><label>Unit:</label> <select name=units><option value=metric>Metric<option value=imperial>Imperial<option value=standard>Standard</select></div><div><label>Hour Mode:</label> <select name=hour_mode><option value=24>24 Hours<option value=12>12 Hours</select></div><div><label>Language:</label> <select name=lang><option value=sq>Albanian<option value=af>Afrikaans<option value=ar>Arabic<option value=az>Azerbaijani<option value=eu>Basque<option value=be>Belarusian<option value=bg>Bulgarian<option value=ca>Catalan<option value=zh_cn>Chinese Simplified<option value=zh_tw>Chinese Traditional<option value=hr>Croatian<option value=cz>Czech<option value=da>Danish<option value=nl>Dutch<option value=en>English<option value=fi>Finnish<option value=fr>French<option value=gl>Galician<option value=de>German<option value=el>Greek<option value=he>Hebrew<option value=hi>Hindi<option value=hu>Hungarian<option value=is>Icelandic<option value=id>Indonesian<option value=it>Italian<option value=ja>Japanese<option value=kr>Korean<option value=ku>Kurmanji (Kurdish)<option value=la>Latvian<option value=lt>Lithuanian<option value=mk>Macedonian<option value=no>Norwegian<option value=fa>Persian (Farsi)<option value=pl>Polish<option value=pt>Portugu&ecirc;s (Portugal)<option value=pt_br>Portugu&ecirc;s<option value=ro>Romanian<option value=ru>Russian<option value=sr>Serbian<option value=sk>Slovak<option value=sl>Slovenian<option value="sp, es">Spanish<option value="sv, se">Swedish<option value=th>Thai<option value=tr>Turkish<option value="ua, uk">Ukrainian<option value=vi>Vietnamese<option value=zu>Zulu</select></div><div><label>Select your timezone:</label> <select name=tmz><option value=-12:00>(UTC -12:00) Baker Island<option value=-11:00>(UTC -11:00) Pago Pago, American Samoa<option value=-10:00>(UTC -10:00) Honolulu, Hawaii<option value=-09:00>(UTC -09:00) Anchorage, Alaska<option value=-08:00>(UTC -08:00) Los Angeles, Vancouver<option value=-07:00>(UTC -07:00) Denver, Phoenix<option value=-06:00>(UTC -06:00) Chicago, Mexico City<option value=-05:00>(UTC -05:00) New York, Toronto, Bogotá<option value=-04:00>(UTC -04:00) Santiago, Caracas<option value=-03:00 selected>(UTC -03:00) Buenos Aires, S&atilde;o Paulo<option value=-02:00>(UTC -02:00) South Georgia<option value=-01:00>(UTC -01:00) Azores, Cape Verde<option value=+00:00>(UTC +00:00) London, Lisbon<option value=+01:00>(UTC +01:00) Berlin, Paris, Rome<option value=+02:00>(UTC +02:00) Athens, Cairo, Johannesburg<option value=+03:00>(UTC +03:00) Moscow, Nairobi, Baghdad<option value=+04:00>(UTC +04:00) Dubai, Baku<option value=+05:00>(UTC +05:00) Karachi, Tashkent<option value=+05:30>(UTC +05:30) New Delhi, Colombo<option value=+06:00>(UTC +06:00) Almaty, Dhaka<option value=+07:00>(UTC +07:00) Bangkok, Jakarta<option value=+08:00>(UTC +08:00) Beijing, Singapore, Perth<option value=+09:00>(UTC +09:00) Tokyo, Seoul<option value=+09:30>(UTC +09:30) Darwin, Adelaide<option value=+10:00>(UTC +10:00) Sydney, Brisbane<option value=+11:00>(UTC +11:00) Solomon Islands, Noum&eacute;a<option value=+12:00>(UTC +12:00) Auckland, Fiji</select></div><div><label>Latitude</label> <input name=lat></div><div><label>Longitude</label> <input name=lon></div><br><h1>Configure Home Assistant Integration</h1><hr><div><label>Home Assistant Address</label> <input name=ha_address></div><div><label>Home Assistant Token</label> <input name=ha_token></div><div><label>Sensor 1 Entity ID (Temperature)*</label> <input name=sensor1_temp></div><div><label>Sensor 1 Entity ID (Humidity)</label> <input name=sensor1_h></div><div><label>Sensor 2 Entity ID (Temperature)*</label> <input name=sensor2_temp></div><div><label>Sensor 2 Entity ID (Humidity)</label> <input name=sensor2_h></div><div><label>Sensor 3 Entity ID (Temperature)*</label> <input name=sensor3_temp></div><div><label>Sensor 3 Entity ID (Humidity)</label> <input name=sensor3_h></div><br><small>* Home Assistant sensors must be set to return value in celsius, the conversion for Fahrenheit or Kelvin is done on the display itself.</small><hr><div><input class=action-button type=submit value=Save><div></fieldset></form>)rawliteral");
  });

  server.on("/save", HTTP_GET, []() {
    String units = server.arg("units");
    String lang = server.arg("lang");
    String hrMode = server.arg("hour_mode");
    String tz = server.arg("tmz");
    String lon = server.arg("lon");
    String lat = server.arg("lat");
    String haTk = server.arg("ha_token");
    String haAd = server.arg("ha_address");
    String owTk = server.arg("ow_token");
    String owSensor1Temp = server.arg("sensor1_temp");
    String owSensor1H = server.arg("sensor1_h");
    String owSensor2Temp = server.arg("sensor2_temp");
    String owSensor2H = server.arg("sensor2_h");
    String owSensor3Temp = server.arg("sensor3_temp");
    String owSensor3H = server.arg("sensor3_h");
    
    // Save settings
    preferences.putString("units", units);
    preferences.putString("lang", lang);
    preferences.putString("hour_mode", hrMode);
    preferences.putString("lon", lon);
    preferences.putString("lat", lat);
    preferences.putString("tmz", tz);
    preferences.putString("ha_address", haAd);
    preferences.putString("ha_token", haTk);
    preferences.putString("ow_token", owTk);
    preferences.putString("sensor1_temp", owSensor1Temp);
    preferences.putString("sensor1_h", owSensor1H);
    preferences.putString("sensor2_temp", owSensor2Temp);
    preferences.putString("sensor2_h", owSensor2H);
    preferences.putString("sensor3_temp", owSensor3Temp);
    preferences.putString("sensor3_h", owSensor3H);
    preferences.putBool("configured", true);
    server.send(200, "text/html", R"rawliteral(<body><style>@import url(https://fonts.googleapis.com/css?family=Montserrat);*{margin:0;padding:0}html{height:auto}body{font-family:montserrat,arial,verdana;height:auto;width:100%;background:linear-gradient(rgba(196,102,0,.6),rgba(155,89,182,.6))}hr{margin:10px 0}#prf{width:90vw;height:fit-content;max-width:1200px;margin:50px auto;text-align:center;position:relative}#prf fieldset{background:#fff;border:0;border-radius:.35rem;box-shadow:0 0 15px 1px rgba(0,0,0,.4);padding:20px 30px;box-sizing:border-box;width:80%;margin:0 10%;position:relative}#prf fieldset>div{display:flex;margin:10px 0}#prf fieldset>div>input,#prf fieldset>div>select,#prf fieldset>div>textarea{margin:auto}#prf fieldset>div>label{margin:auto 5px}h1{font-size:15px;text-transform:uppercase;color:#2c3e50;margin-bottom:10px}</style><form action=/save id=prf><fieldset><h1>Preferences saved successfully!</h1><hr><div><label>Now the device will reboot and should start showing the current weather data from your location and the Home Assistant sensors data.</label></div><div><label>If any of the configurations set here are incorrect, please reset the device by pressing the RESET button and the RELOAD button at the same time.</label></div></fieldset></form>)rawliteral");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

// Clear user preferences
void clearPreferences(){
  preferences.begin("user-prefs", false);
  preferences.clear();
  preferences.end();
}

void managePreferences(){
  preferences.begin("user-prefs", false);
  firstBoot = !preferences.getBool("configured", false);
  if (firstBoot) {
    Serial.println("First boot - starting config server...");
    startConfigServer();
    String ipAddress = WiFi.localIP().toString();
    String setupInfo = "\nGetting Started\n\nNow that the device is connected\nto the wifi, you'll need\nto configure it.\n\nPlease, go to http://" + ipAddress + "\non your internet browser,\nor point your phone to the QRCode\naside, to finalize the setup.";
    showFullScrMessage(setupInfo, "http://" + ipAddress);
    while (true) {
      server.handleClient();
      delay(10);
    }
  } else {
    Serial.println("Normal boot");
    loadPreferences();
  }
}

void blinkLED(int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(speed);
    digitalWrite(STATUS_LED, LOW);
    delay(speed);
  }
}

void goto_deep_sleep() {
  display.hibernate();
  delay(100);
  display.powerOff();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();
  vTaskDelay(10);
  esp_sleep_enable_timer_wakeup(SLEEP_INTERVAL_SEC * 1000000ULL);
  Serial.println("Going to sleep now...");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  analogReadResolution(12);

  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  Serial.printf("Wake reason: %d\n", wakeupReason);
  if (wakeupReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.printf("Woke up from deep sleep, count: %d\n", wakeCount);
  } else {
    Serial.println("First boot or manual reset");
    wakeCount = 0;
  }

  // Check if woken up by GPIO (RESET button)
  if (digitalRead(RESET_PIN) == LOW) {
    triggerReset = true;
  }

  // Init display
  SPI.begin(SCK, MISO, MOSI, CS);
  display.init(115200, true, 50, false);

  preferences.begin("wifi", false);
  bool wasConnectedBefore = preferences.getBool("wifi_success", false);

  // Handle Wi-Fi reset
  if (triggerReset) {
    String resetMessage = getResetMessage("Wi-Fi credentials and settings have been reset!", String(AP_SSID), String(AP_PASSWORD));
    String wifiStr = "WIFI:T:WPA;S:" + String(AP_SSID) + ";P:" + String(AP_PASSWORD) + ";;";
    clearPreferences();
    preferences.putBool("wifi_success", false);
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    Serial.println(resetMessage);
    blinkLED(3, 500);
    showFullScrMessage(resetMessage, wifiStr);
    triggerReset = false;
    delay(100);
  }

  // Start Wi-Fi connection
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 3) {
      delay(1000);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected after retry.");
      preferences.putBool("wifi_success", true);
      blinkLED(2, 40);
      managePreferences();
    } else {
      Serial.println("\nConnection failed.");

      if (wasConnectedBefore) {
        Serial.println("Previously connected. Skipping AP mode and going to sleep.");
        blinkLED(5, 200);
        goto_deep_sleep();
        return;
      }

      // First-time setup or after reset: fallback to AP mode
      String resetMessage = getResetMessage("Please, connect to the AP to configure a network!", String(AP_SSID), String(AP_PASSWORD));
      String wifiStr = "WIFI:T:WPA;S:" + String(AP_SSID) + ";P:" + String(AP_PASSWORD) + ";;";
      showFullScrMessage(resetMessage, wifiStr);
      Serial.println(resetMessage);
      blinkLED(3, 500);

      WiFiManager wifiManager;
      if (wifiManager.autoConnect(AP_SSID, AP_PASSWORD)) {
        preferences.putBool("wifi_success", true);
      } else {
        Serial.println("WiFiManager failed. Going to sleep.");
        goto_deep_sleep();
        return;
      }
    }
  } else {
    blinkLED(2, 40);  // Already connected
  }

  // Print connection info
  String wifiConnectedInfo = "\nWi-fi connected!\n\nIP: " + WiFi.localIP().toString() +
                             "\n\nSSID: " + WiFi.SSID() + "\n\nSignal: " + String(WiFi.RSSI()) + "dBm";
  Serial.println(wifiConnectedInfo);

  if (wakeupReason == 0) {
    if (WiFi.status() != WL_CONNECTED) {
      String resetMessage = getResetMessage("Please, connect to the AP to configure a network!", String(AP_SSID), String(AP_PASSWORD));
      String wifiStr = "WIFI:T:WPA;S:" + String(AP_SSID) + ";P:" + String(AP_PASSWORD) + ";;";
      showFullScrMessage(resetMessage, wifiStr);
    } else {
      showFullScrMessage(wifiConnectedInfo, "");
    }
  }

  renderInfo();

  wakeCount++;

  goto_deep_sleep();
}

void loop() {
  // Not used
}