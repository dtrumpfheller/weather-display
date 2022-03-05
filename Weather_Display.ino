#ifndef BOARD_HAS_PSRAM
  #error "Please enable PSRAM !!!"
#endif

#include <Arduino.h>
#include <esp_adc_cal.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <epd_driver.h>  // download: https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
#include <ArduinoJson.h> // download: https://github.com/bblanchon/ArduinoJson

#include "forecast_record.h"
#include "lang.h"
#include "opensans8b.h"
#include "opensans10b.h"
#include "opensans12b.h"
#include "opensans18b.h"
#include "opensans24b.h"
#include "moon.h"
#include "sunrise.h"
#include "sunset.h"
#include "uvi.h"
#include "properties.h" // please modify this file to your own situation

// --------------------------------

#define SCREEN_WIDTH  EPD_WIDTH  // 960
#define SCREEN_HEIGHT EPD_HEIGHT // 540

#define White     0xFF
#define LightGrey 0xBB
#define Grey      0x88
#define DarkGrey  0x44
#define Black     0x00

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

#define max_readings 40 // 40 for 5-days

#define Large 20 // for icon drawing
#define Small 10 // for icon drawing

#define PAGE_WEATHER 1
#define PAGE_HISTORY 2

// --------------------------------

enum alignment {LEFT, RIGHT, CENTER};

typedef struct { // for current Day and Day 1, 2, 3, etc
  String Time;
  float  High;
  float  Low;
} HL_record_type;

typedef struct { // for history data
  String location;
  boolean hasTemperature;
  boolean hasHumidity;
  boolean hasCo2;
  float temperature[max_readings];
  float humidity[max_readings];
  float co2[max_readings];
} HistoryData;

// --------------------------------

RTC_DATA_ATTR int page = PAGE_WEATHER;

// --------------------------------

HL_record_type HLReadings[max_readings];

const boolean LargeIcon = true;
const boolean SmallIcon = false;

String City     = "";
String Time_str = "--:--:--";
String Date_str = "-- --- ----";

String ForecastDay;

int wifi_signal;
int CurrentHour = 0;
int CurrentMin  = 0;
int CurrentSec  = 0;
int EventCnt    = 0;
int vref        = 1100;

Forecast_record_type  WxConditions[1];
Forecast_record_type  WxForecast[max_readings];

float pressure_readings[max_readings]    = {0};
float temperature_readings[max_readings] = {0};
float humidity_readings[max_readings]    = {0};
float rain_readings[max_readings]        = {0};
float snow_readings[max_readings]        = {0};

int numberLocations = 0;
HistoryData historyData[3];

long StartTime  = 0;

GFXfont currentFont;

uint8_t *framebuffer;

// --------------------------------

void setup() {
  initialiseSystem();
  if (startWiFi() && setupTime()) {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0){
      // switch between pages
      if (page == PAGE_WEATHER){
        page = PAGE_HISTORY;
      } else {
        page = PAGE_WEATHER;
      }
    } else {
      // always go to OWM page
      page = PAGE_WEATHER;
    }

    // show desired page
    if (page == PAGE_WEATHER){
      showWeather();
    } else {
      showHistory();
    }
  }
  beginSleep();
}

void loop() {
  // nothing to do here
}

void initialiseSystem() {
  StartTime = millis();
  setCpuFrequencyMhz(160); // lower power consumption by sacrificing some performance
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\nInitializing...");
  epd_init();
  framebuffer = (uint8_t *) ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

boolean setupTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  return updateLocalTime();
}

boolean updateLocalTime() {
  struct tm timeinfo;
  char time_output[30], day_output[30], update_time[30];
  while (!getLocalTime(&timeinfo, 5000)) { // wait for 5s for time to synchronise
    Serial.println("Failed to obtain time!");
    return false;
  }
  CurrentHour = timeinfo.tm_hour;
  CurrentMin  = timeinfo.tm_min;
  CurrentSec  = timeinfo.tm_sec;
  //See http://www.cplusplus.com/reference/ctime/strftime/
  Serial.print("Current time: ");
  Serial.println(&timeinfo, "%a %b %d %Y %H:%M:%S"); // displays: Sat Jan 01 2022 23:18:12
  if (Units == "M") {
    // sprintf(day_output, "%s, %02u %s %04u", weekday_D[timeinfo.tm_wday], timeinfo.tm_mday, month_M[timeinfo.tm_mon], (timeinfo.tm_year) + 1900);
    strftime(day_output, sizeof(day_output), "%a %b %d %Y", &timeinfo); // Creates  'Sat Jan 01 2022'
    strftime(update_time, sizeof(update_time), "%H:%M:%S", &timeinfo);  // Creates: '@ 14:05:49' and change from 30 to 8
    sprintf(time_output, "%s", update_time);
  } else {
    strftime(day_output, sizeof(day_output), "%a %b-%d-%Y", &timeinfo); // Creates  'Sat May-31-2019'
    strftime(update_time, sizeof(update_time), "%r", &timeinfo);        // Creates: '@ 02:05:49pm'
    sprintf(time_output, "%s", update_time);
  }
  Date_str = day_output;
  Time_str = time_output;
  return true;
}

boolean startWiFi() {
  Serial.println("Connecting to: " + String(ssid));
  WiFi.mode(WIFI_STA);
  if (staticIP) WiFi.config(localIP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < wifiTimeout) { // timeout
    delay(10);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected at: " + WiFi.localIP().toString());
    wifi_signal = WiFi.RSSI(); // get Wifi Signal strength now, because the WiFi will be turned off soon to save power
    return true;
  } else {
    Serial.println("WiFi connection *** FAILED ***");
    return false;
  }
}

void stopWiFi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi switched Off");
}

void beginSleep() {
  // configure GPIO34 as ext0 wake up source for LOW logic level
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, 0); 

  // configure timer
  long sleepTimer = (sleepDuration * 60 - ((CurrentMin % sleepDuration) * 60 + CurrentSec));
  if (sleepTimer < 300) {
    // some ESP32 have a RTC that is too fast to maintain accurate time
    // so skip next wake up if it is within next 5 minutes
    sleepTimer += sleepDuration * 60;
  }
  esp_sleep_enable_timer_wakeup(sleepTimer * 1000000LL); // in seconds, 1000000LL converts to seconds as unit = 1us

  Serial.println("Awake for " + String((millis() - StartTime) / 1000.0, 3) + "s");
  Serial.println("Entering " + String(sleepTimer) + "s of sleep time");
  Serial.println("Starting deep-sleep period...");

  esp_deep_sleep_start();
}

void showWeather() {
  bool wakeUp = false;
  if (wakeupHour > sleepHour){
    wakeUp = (CurrentHour >= wakeupHour || CurrentHour <= sleepHour);
  } else {
    wakeUp = (CurrentHour >= wakeupHour && CurrentHour <= sleepHour);
  }

  if (wakeUp) {
    byte attempts = 1;
    bool RxWeather  = false;
    bool RxForecast = false;
    WiFiClient client;
    while ((RxWeather == false || RxForecast == false) && attempts <= 2) { // try up-to 2 time for Weather and Forecast data
      if (RxWeather == false) RxWeather = obtainWeatherData(client, "onecall");
      if (RxForecast == false) RxForecast = obtainWeatherData(client, "forecast");
      attempts++;
    }
    stopWiFi(); // not needed anymore, reduces power consumption
    if (RxWeather && RxForecast) { // only if received both Weather or Forecast proceed
      Serial.println("Received and decoded all weather data from OWM...");
      epd_poweron();      // switch on EPD display
      epd_clear();        // clear the screen
      DisplayWeather();   // write the weather data to framebuffer, except moon image which is painted directly in order to be able to overlay ith with the current status
      epd_draw_grayscale_image(epd_full_screen(), framebuffer); // update the screen from framebuffer
      epd_poweroff();     // switch off all power to EPD
      epd_poweroff_all(); // switch off all power to EPD
    }
  } else {
    Serial.println("Not time yet for weather update");
  }
}

void showHistory() {
  // user had to manually navigate to this page, so always wake up
  byte attempts = 1;
  bool RxWeather = false;
  WiFiClient client;
  while (RxWeather == false && attempts <= 2) { // try up-to 2 times
    RxWeather = obtainHistoryData(client);
    attempts++;
  }
  stopWiFi(); // not needed anymore, reduces power consumption

  if (RxWeather){
    Serial.println("Received and decoded all weather data from InfluxDB2...");
    if (numberLocations > 0){
      drawLocationHistory(40, historyData[0]);
    } if (numberLocations > 1){
      drawLocationHistory(220, historyData[1]);
    } if (numberLocations > 2){
      drawLocationHistory(400, historyData[2]);
    }
    
    epd_poweron();      // switch on EPD display
    epd_clear();        // clear the screen
    epd_draw_grayscale_image(epd_full_screen(), framebuffer); // update the screen from framebuffer
    epd_poweroff();     // switch off all power to EPD
    epd_poweroff_all();
  }
}

void drawLocationHistory(int y, HistoryData data) {
  int gwidth = 175, gheight = 100;
  setFont(OpenSans10B);
  drawString(15, y +45, data.location, LEFT);
  if (data.hasTemperature) {
    DrawGraph(265, y, 175, gheight, 10, 30, Units == "M" ? TXT_TEMPERATURE_C : TXT_TEMPERATURE_F, data.temperature, max_readings, autoscale_on, barchart_off, historyDays, true);
  }
  if (data.hasHumidity) {
    DrawGraph(515, y, 175, gheight, 0, 100, TXT_HUMIDITY_PERCENT, data.humidity, max_readings, autoscale_off, barchart_off, historyDays, true);
  }
  if (data.hasCo2) {
    DrawGraph(765, y, gwidth, gheight, 0, 3000, TXT_CO2, data.co2, max_readings, autoscale_on, barchart_off, historyDays, true);
  }
}

bool obtainHistoryData(WiFiClient & client) {
  Serial.println("Requesting weather history");
  client.stop(); // close connection before sending a new request
  HTTPClient http;
  http.begin(client, influxDb2Agent +"/weather");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    if (!decodeHistory(http.getStream())) return false;
    client.stop();
  } else {
    Serial.printf("connection failed, error: %s\n", http.errorToString(httpCode).c_str());
    client.stop();
    http.end();
    return false;
  }
  http.end();
  return true;
}

bool decodeHistory(WiFiClient& json) {
  DynamicJsonDocument doc(64 * 1024);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }
  JsonArray root = doc.as<JsonArray>();
  Serial.println("Decoding InfluxDB2 weather data");
  int locationCounter = 0;
  numberLocations = root.size();
  for (JsonObject location : root) {
    historyData[locationCounter].location = location["location"].as<String>();
    Serial.println("Mapping location: " + historyData[locationCounter].location);
    if (location.containsKey("temperature")){
      JsonArray temperature = location["temperature"];
      if (temperature.size() > 0){
        int tempCounter = 0;
        for (float value : temperature) {
          if (tempCounter < max_readings){
            historyData[locationCounter].temperature[tempCounter] = value;
          }
          tempCounter++;
        }
        historyData[locationCounter].hasTemperature = true;
      } else {
        historyData[locationCounter].hasTemperature = false;
      }
    }
    if (location.containsKey("humidity")){
      JsonArray humidity = location["humidity"];
      if (humidity.size() > 0){
        int humidityCounter = 0;
        for (int value : humidity) {
          if (humidityCounter < max_readings){
            historyData[locationCounter].humidity[humidityCounter] = value;
          }
          humidityCounter++;
        }
        historyData[locationCounter].hasHumidity = true;
      } else {
        historyData[locationCounter].hasHumidity = false;
      }
    }
    if (location.containsKey("co2")){
      JsonArray co2 = location["co2"];
      if (co2.size() > 0){
        int co2Counter = 0;
        for (int value : co2) {
          if (co2Counter < max_readings){
            historyData[locationCounter].co2[co2Counter] = value;
          }
          co2Counter++;
        }
        historyData[locationCounter].hasCo2 = true;
      } else {
        historyData[locationCounter].hasCo2 = false;
      }
    }
    locationCounter++;
  }

  return true;
}

void Convert_Readings_to_Imperial() {
  WxConditions[0].Pressure = hPa_to_inHg(WxConditions[0].Pressure);
  WxForecast[0].Rainfall   = mm_to_inches(WxForecast[0].Rainfall);
  WxForecast[0].Snowfall   = mm_to_inches(WxForecast[0].Snowfall);
}

bool DecodeWeather(WiFiClient& json, String Type) {
  DynamicJsonDocument doc(64 * 1024);                      // allocate the JsonDocument
  DeserializationError error = deserializeJson(doc, json); // deserialize the JSON document
  if (error) {                                             // test if parsing succeeds.
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }
  // convert it to a JsonObject
  JsonObject root = doc.as<JsonObject>();
  Serial.println("Decoding " + Type + " weather data");
  if (Type == "onecall") {
    WxConditions[0].High        = -50; // maximum forecast high
    WxConditions[0].Low         = 50;  // minimum forecast low
    WxConditions[0].FTimezone   = doc["timezone_offset"];
    JsonObject current          = doc["current"];
    WxConditions[0].Sunrise     = current["sunrise"];
    WxConditions[0].Sunset      = current["sunset"];
    WxConditions[0].Temperature = current["temp"];
    WxConditions[0].FeelsLike   = current["feels_like"];
    WxConditions[0].Pressure    = current["pressure"];
    WxConditions[0].Humidity    = current["humidity"];
    WxConditions[0].DewPoint    = current["dew_point"];
    WxConditions[0].UVI         = current["uvi"];
    WxConditions[0].Cloudcover  = current["clouds"];
    WxConditions[0].Visibility  = current["visibility"];
    WxConditions[0].Windspeed   = current["wind_speed"];
    WxConditions[0].Winddir     = current["wind_deg"];
    JsonObject current_weather  = current["weather"][0];
    WxConditions[0].Forecast0   = current_weather["description"].as<String>(); // "scattered clouds"
    WxConditions[0].Icon        = current_weather["icon"].as<String>();        // "01n"
  }
  if (Type == "forecast") {
    City                              = root["city"]["name"].as<String>();
    JsonArray list                    = root["list"];
    for (byte r = 0; r < max_readings; r++) {
      WxForecast[r].Dt                = list[r]["dt"].as<int>();
      WxForecast[r].Temperature       = list[r]["main"]["temp"].as<float>();
      WxForecast[r].Low               = list[r]["main"]["temp_min"].as<float>();
      WxForecast[r].High              = list[r]["main"]["temp_max"].as<float>();
      WxForecast[r].Pressure          = list[r]["main"]["pressure"].as<float>();
      WxForecast[r].Humidity          = list[r]["main"]["humidity"].as<float>();
      WxForecast[r].Icon              = list[r]["weather"][0]["icon"].as<const char*>();
      WxForecast[r].Rainfall          = list[r]["rain"]["3h"].as<float>();
      WxForecast[r].Snowfall          = list[r]["snow"]["3h"].as<float>();
      if (r < 8) { // check next 3 x 8 hours = 1 day
        if (WxForecast[r].High > WxConditions[0].High) WxConditions[0].High = WxForecast[r].High; // get highest temperature for next 24Hrs
        if (WxForecast[r].Low  < WxConditions[0].Low)  WxConditions[0].Low  = WxForecast[r].Low;  // get lowest temperature for next 24Hrs
      }
    }
    GetHighsandLows();
    //------------------------------------------
    float pressure_trend = WxForecast[0].Pressure - WxForecast[2].Pressure; // measure pressure slope between ~now and later
    pressure_trend = ((int)(pressure_trend * 10)) / 10.0; // remove any small variations less than 0.1
    WxConditions[0].Trend = "=";
    if (pressure_trend > 0)  WxConditions[0].Trend = "+";
    if (pressure_trend < 0)  WxConditions[0].Trend = "-";
    if (pressure_trend == 0) WxConditions[0].Trend = "0";

    if (Units == "I") Convert_Readings_to_Imperial();
  }
  return true;
}

bool obtainWeatherData(WiFiClient & client, const String & RequestType) {
  Serial.println("Requesting " +RequestType + " weather data");
  const String units = (Units == "M" ? "metric" : "imperial");
  client.stop(); // close connection before sending a new request
  HTTPClient http;
  //api.openweathermap.org/data/2.5/RequestType?lat={lat}&lon={lon}&appid={API key}
  String uri = "/data/2.5/" + RequestType + "?lat=" + latitude + "&lon=" + longitude + "&appid=" + apikey + "&mode=json&units=" + units + "&lang=" + language;
  if (RequestType == "onecall") uri += "&exclude=minutely,hourly,alerts,daily";
  http.begin(client, server, 80, uri); //http.begin(uri,test_root_ca); //HTTPS example connection
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    if (!DecodeWeather(http.getStream(), RequestType)) return false;
    client.stop();
  } else {
    Serial.printf("connection failed, error: %s\n", http.errorToString(httpCode).c_str());
    client.stop();
    http.end();
    return false;
  }
  http.end();
  return true;
}

float mm_to_inches(float value_mm) {
  return 0.0393701 * value_mm;
}

float hPa_to_inHg(float value_hPa) {
  return 0.02953 * value_hPa;
}

int JulianDate(int d, int m, int y) {
  int mm, yy, k1, k2, k3, j;
  yy = y - (int)((12 - m) / 10);
  mm = m + 9;
  if (mm >= 12) mm = mm - 12;
  k1 = (int)(365.25 * (yy + 4712));
  k2 = (int)(30.6001 * mm + 0.5);
  k3 = (int)((int)((yy / 100) + 49) * 0.75) - 38;
  // 'j' for dates in Julian calendar:
  j = k1 + k2 + d + 59 + 1;
  if (j > 2299160) j = j - k3; // 'j' is the Julian date at 12h UT (Universal Time) For Gregorian calendar:
  return j;
}

float SumOfPrecip(float DataArray[], int readings) {
  float sum = 0;
  for (int i = 0; i < readings; i++) sum += DataArray[i];
  return sum;
}

String TitleCase(String text) {
  if (text.length() > 0) {
    String temp_text = text.substring(0, 1);
    temp_text.toUpperCase();
    return temp_text + text.substring(1); // title-case the string
  }
  else return text;
}

void DisplayWeather() {                          // 4.7" e-paper display is 960x540 resolution
  DisplayStatusSection(600, 20, wifi_signal);    // Wi-Fi signal strength and Battery voltage
  DisplayGeneralInfoSection();                   // Top line of the display
  DisplayDisplayWindSection(137, 150, WxConditions[0].Winddir, WxConditions[0].Windspeed, 100);
  DisplayAstronomySection(5, 255);               // Astronomy section Sun rise/set, Moon phase and Moon icon
  DisplayMainWeatherSection(320, 110);           // Centre section of display for Location, temperature, Weather report, current Wx Symbol
  DisplayWeatherIcon(835, 140);                  // Display weather icon scale = Large;
  DisplayForecastSection(285, 220);              // 3hr forecast boxes
  DisplayGraphSection(320, 220);                 // Graphs of pressure, temperature, humidity and rain or snowfall
}

void DisplayGeneralInfoSection() {
  setFont(OpenSans10B);
  drawString(5, 2, City, LEFT);
  setFont(OpenSans8B);
  drawString((Units == "M" ? 500 : 480), 4, Date_str + "  @  " + Time_str, LEFT);
}

void DisplayWeatherIcon(int x, int y) {
  DisplayConditionsSection(x, y, WxConditions[0].Icon, LargeIcon);
}

void DisplayMainWeatherSection(int x, int y) {
  setFont(OpenSans8B);
  DisplayTempHumiPressSection(x, y - 60);
  DisplayForecastTextSection(x - 55, y + 45);
  DisplayVisiCCoverUVISection(x - 10, y + 95);
}

void DisplayDisplayWindSection(int x, int y, float angle, float windspeed, int cradius) {
  arrow(x, y, cradius - 22, angle, 18, 33); // Show wind direction on outer circle of width and length
  setFont(OpenSans8B);
  int dxo, dyo, dxi, dyi;
  drawCircle(x, y, cradius, Black);       // Draw compass circle
  drawCircle(x, y, cradius + 1, Black);   // Draw compass circle
  drawCircle(x, y, cradius * 0.7, Black); // Draw compass inner circle
  for (float a = 0; a < 360; a = a + 22.5) {
    dxo = cradius * cos((a - 90) * PI / 180);
    dyo = cradius * sin((a - 90) * PI / 180);
    if (a == 45)  drawString(dxo + x + 15, dyo + y - 18, TXT_NE, CENTER);
    if (a == 135) drawString(dxo + x + 20, dyo + y - 2,  TXT_SE, CENTER);
    if (a == 225) drawString(dxo + x - 20, dyo + y - 2,  TXT_SW, CENTER);
    if (a == 315) drawString(dxo + x - 15, dyo + y - 18, TXT_NW, CENTER);
    dxi = dxo * 0.9;
    dyi = dyo * 0.9;
    drawLine(dxo + x, dyo + y, dxi + x, dyi + y, Black);
    dxo = dxo * 0.7;
    dyo = dyo * 0.7;
    dxi = dxo * 0.9;
    dyi = dyo * 0.9;
    drawLine(dxo + x, dyo + y, dxi + x, dyi + y, Black);
  }
  drawString(x, y - cradius - 20,     TXT_N, CENTER);
  drawString(x, y + cradius + 10,     TXT_S, CENTER);
  drawString(x - cradius - 15, y - 5, TXT_W, CENTER);
  drawString(x + cradius + 10, y - 5, TXT_E, CENTER);
  drawString(x + 3, y + 50, String(angle, 0) + "°", CENTER);
  setFont(OpenSans12B);
  drawString(x, y - 50, WindDegToOrdinalDirection(angle), CENTER);
  setFont(OpenSans24B);
  drawString(x + 3, y - 18, String(windspeed, 1), CENTER);
  setFont(OpenSans12B);
  drawString(x, y + 22, (Units == "M" ? "m/s" : "mph"), CENTER);
}

String WindDegToOrdinalDirection(float winddirection) {
  if (winddirection >= 348.75 || winddirection < 11.25)  return TXT_N;
  if (winddirection >=  11.25 && winddirection < 33.75)  return TXT_NNE;
  if (winddirection >=  33.75 && winddirection < 56.25)  return TXT_NE;
  if (winddirection >=  56.25 && winddirection < 78.75)  return TXT_ENE;
  if (winddirection >=  78.75 && winddirection < 101.25) return TXT_E;
  if (winddirection >= 101.25 && winddirection < 123.75) return TXT_ESE;
  if (winddirection >= 123.75 && winddirection < 146.25) return TXT_SE;
  if (winddirection >= 146.25 && winddirection < 168.75) return TXT_SSE;
  if (winddirection >= 168.75 && winddirection < 191.25) return TXT_S;
  if (winddirection >= 191.25 && winddirection < 213.75) return TXT_SSW;
  if (winddirection >= 213.75 && winddirection < 236.25) return TXT_SW;
  if (winddirection >= 236.25 && winddirection < 258.75) return TXT_WSW;
  if (winddirection >= 258.75 && winddirection < 281.25) return TXT_W;
  if (winddirection >= 281.25 && winddirection < 303.75) return TXT_WNW;
  if (winddirection >= 303.75 && winddirection < 326.25) return TXT_NW;
  if (winddirection >= 326.25 && winddirection < 348.75) return TXT_NNW;
  return "?";
}

void DisplayTempHumiPressSection(int x, int y) {
  setFont(OpenSans18B);
  drawString(x - 30, y, String(WxConditions[0].Temperature, 1) + "°   " + String(WxConditions[0].Humidity, 0) + "%", LEFT);
  setFont(OpenSans12B);
  DrawPressureAndTrend(x + 195, y + 15, WxConditions[0].Pressure, WxConditions[0].Trend);
  int Yoffset = 42;
  if (WxConditions[0].Windspeed > 0) {
    drawString(x - 30, y + Yoffset, String(WxConditions[0].FeelsLike, 1) + "° FL", LEFT);   // Show FeelsLike temperature if windspeed > 0
    Yoffset += 30;
  }
  drawString(x - 30, y + Yoffset, String(WxConditions[0].High, 0) + "° | " + String(WxConditions[0].Low, 0) + "° Hi/Lo", LEFT); // Show forecast high and Low
}

void DisplayForecastTextSection(int x, int y) {
#define lineWidth 34
  setFont(OpenSans12B);
  String Wx_Description = WxConditions[0].Forecast0;
  Wx_Description.replace(".", ""); // remove any '.'
  int spaceRemaining = 0, p = 0, charCount = 0, Width = lineWidth;
  while (p < Wx_Description.length()) {
    if (Wx_Description.substring(p, p + 1) == " ") spaceRemaining = p;
    if (charCount > Width - 1) { // '~' is the end of line marker
      Wx_Description = Wx_Description.substring(0, spaceRemaining) + "~" + Wx_Description.substring(spaceRemaining + 1);
      charCount = 0;
    }
    p++;
    charCount++;
  }
  if (WxForecast[0].Rainfall > 0) Wx_Description += " (" + String(WxForecast[0].Rainfall, 1) + String((Units == "M" ? "mm" : "in")) + ")";
  String Line1 = Wx_Description.substring(0, Wx_Description.indexOf("~"));
  String Line2 = Wx_Description.substring(Wx_Description.indexOf("~") + 1);
  drawString(x + 30, y + 5, TitleCase(Line1), LEFT);
  if (Line1 != Line2) drawString(x + 30, y + 30, Line2, LEFT);
}

void DisplayVisiCCoverUVISection(int x, int y) {
  setFont(OpenSans12B);
  Visibility(x + 5, y, String(WxConditions[0].Visibility) + "M");
  CloudCover(x + 155, y, WxConditions[0].Cloudcover);
  Display_UVIndexLevel(x + 265, y, WxConditions[0].UVI);
}

void Display_UVIndexLevel(int x, int y, float UVI) {
  String Level = "";
  if (UVI <  2)              Level = " (L)";
  if (UVI >= 2 && UVI <  5)  Level = " (M)";
  if (UVI >= 5 && UVI <  7)  Level = " (H)";
  if (UVI >= 7 && UVI <  10) Level = " (VH)";
  if (UVI >= 10)             Level = " (EX)";
  drawString(x + 20, y - 3, String(UVI, (UVI < 0 ? 1 : 0)) + Level, LEFT);
  DrawUVI(x - 10, y - 5);
}

void DisplayAstronomySection(int x, int y) {
  setFont(OpenSans10B);
  time_t now = time(NULL);
  struct tm * now_utc  = gmtime(&now);
  drawString(x + 5, y + 105, MoonPhase(now_utc->tm_mday, now_utc->tm_mon + 1, now_utc->tm_year + 1900, Hemisphere), LEFT);
  DrawMoonImage(x + 10, y + 23); // Different references!
  DrawMoon(x - 28, y - 15, 75, now_utc->tm_mday, now_utc->tm_mon + 1, now_utc->tm_year + 1900, Hemisphere); // Spaced at 1/2 moon size, so 10 - 75/2 = -28
  drawString(x + (Units == "M" ? 115 : 105), y + 38, ConvertUnixTime(WxConditions[0].Sunrise).substring(0, (Units == "M" ? 5 : 6)), LEFT); // Sunrise
  drawString(x + (Units == "M" ? 115 : 105), y + 78, ConvertUnixTime(WxConditions[0].Sunset).substring(0, (Units == "M" ? 5 : 6)), LEFT); // Sunset
  DrawSunriseImage(x + 180, y + 20);
  DrawSunsetImage(x + 180, y + 60);
}

void DrawMoon(int x, int y, int diameter, int dd, int mm, int yy, String hemisphere) {
  double Phase = NormalizedMoonPhase(dd, mm, yy);
  hemisphere.toLowerCase();
  if (hemisphere == "south") Phase = 1 - Phase;
  // Draw dark part of moon
  fillCircle(x + diameter - 1, y + diameter, diameter / 2 + 1, DarkGrey);
  const int number_of_lines = 90;
  for (double Ypos = 0; Ypos <= number_of_lines / 2; Ypos++) {
    double Xpos = sqrt(number_of_lines / 2 * number_of_lines / 2 - Ypos * Ypos);
    // Determine the edges of the lighted part of the moon
    double Rpos = 2 * Xpos;
    double Xpos1, Xpos2;
    if (Phase < 0.5) {
      Xpos1 = -Xpos;
      Xpos2 = Rpos - 2 * Phase * Rpos - Xpos;
    }
    else {
      Xpos1 = Xpos;
      Xpos2 = Xpos - 2 * Phase * Rpos + Rpos;
    }
    // Draw light part of moon
    double pW1x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
    double pW1y = (number_of_lines - Ypos)  / number_of_lines * diameter + y;
    double pW2x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
    double pW2y = (number_of_lines - Ypos)  / number_of_lines * diameter + y;
    double pW3x = (Xpos1 + number_of_lines) / number_of_lines * diameter + x;
    double pW3y = (Ypos + number_of_lines)  / number_of_lines * diameter + y;
    double pW4x = (Xpos2 + number_of_lines) / number_of_lines * diameter + x;
    double pW4y = (Ypos + number_of_lines)  / number_of_lines * diameter + y;
    drawLine(pW1x, pW1y, pW2x, pW2y, White);
    drawLine(pW3x, pW3y, pW4x, pW4y, White);
  }
  drawCircle(x + diameter - 1, y + diameter, diameter / 2, Black);
}

String MoonPhase(int d, int m, int y, String hemisphere) {
  int c, e;
  double jd;
  int b;
  if (m < 3) {
    y--;
    m += 12;
  }
  ++m;
  c   = 365.25 * y;
  e   = 30.6  * m;
  jd  = c + e + d - 694039.09;     /* jd is total days elapsed */
  jd /= 29.53059;                        /* divide by the moon cycle (29.53 days) */
  b   = jd;                              /* int(jd) -> b, take integer part of jd */
  jd -= b;                               /* subtract integer part to leave fractional part of original jd */
  b   = jd * 8 + 0.5;                /* scale fraction from 0-8 and round by adding 0.5 */
  b   = b & 7;                           /* 0 and 8 are the same phase so modulo 8 for 0 */
  if (hemisphere == "south") b = 7 - b;
  if (b == 0) return TXT_MOON_NEW;              // New;              0%  illuminated
  if (b == 1) return TXT_MOON_WAXING_CRESCENT;  // Waxing crescent; 25%  illuminated
  if (b == 2) return TXT_MOON_FIRST_QUARTER;    // First quarter;   50%  illuminated
  if (b == 3) return TXT_MOON_WAXING_GIBBOUS;   // Waxing gibbous;  75%  illuminated
  if (b == 4) return TXT_MOON_FULL;             // Full;            100% illuminated
  if (b == 5) return TXT_MOON_WANING_GIBBOUS;   // Waning gibbous;  75%  illuminated
  if (b == 6) return TXT_MOON_THIRD_QUARTER;    // Third quarter;   50%  illuminated
  if (b == 7) return TXT_MOON_WANING_CRESCENT;  // Waning crescent; 25%  illuminated
  return "";
}

void DisplayForecastSection(int x, int y) {
  int Forecast = 0, Dposition = 0;
  do {
    DisplayForecastWeather(x, y, Forecast, Dposition, 82); // x,y cordinates, forecast number, spacing width
    Forecast++;
    Dposition++;
  } while (Forecast <= 2);
  String StartTime  = "08:00" + String((Units == "M"?"":"A"));
  String MidTime    = "09:00" + String((Units == "M"?"":"A"));
  String FinishTime = "10:00" + String((Units == "M"?"":"A"));
  do {
    String Ftime = ConvertUnixTime(WxForecast[Forecast].Dt).substring(0, (Units == "M"?5:6));
    if (Ftime == StartTime || Ftime == MidTime || Ftime == FinishTime) {
      DisplayForecastWeather(x, y, Forecast, Dposition, 82); // x,y cordinates, forecast number, position, spacing width
      Dposition++;
    }
    Forecast++;
  } while (Forecast < 40);
}

void DisplayForecastWeather(int x, int y, int forecast, int dposition, int fwidth) {
  x = x + fwidth * dposition;
  DisplayConditionsSection(x + fwidth / 2 - 5, y + 85, WxForecast[forecast].Icon, SmallIcon);
  setFont(OpenSans10B);
  if (forecast <= 2) {
    drawString(x + fwidth / 2, y + 30, String(ConvertUnixTime(WxForecast[forecast].Dt).substring(0, (Units == "M" ? 5 : 6))), CENTER);
  }
  else
  {
    drawString(x + fwidth / 2 - 3, y + 30, ForecastDay, CENTER);
  }
  if (forecast < 3) drawString(x + fwidth / 2, y + 125, String(WxForecast[forecast].High, 0) + "°/" + String(WxForecast[forecast].Low, 0) + "°", CENTER);
  else drawString(x + fwidth / 2, y + 125, String(HLReadings[dposition - 3].High, 0) + "°/" + String(HLReadings[dposition - 3].Low, 0) + "°", CENTER);
}

double NormalizedMoonPhase(int d, int m, int y) {
  int j = JulianDate(d, m, y);
  //Calculate approximate moon phase
  double Phase = (j + 4.867) / 29.53059;
  return (Phase - (int) Phase);
}

void DisplayGraphSection(int x, int y) {
  int r = 0;
  do { // Pre-load temporary arrays with with data - because C parses by reference and remember that[1] has already been converted to I units
    if (Units == "I") pressure_readings[r] = WxForecast[r].Pressure * 0.02953;   else pressure_readings[r] = WxForecast[r].Pressure;
    if (Units == "I") rain_readings[r]     = WxForecast[r].Rainfall * 0.0393701; else rain_readings[r]     = WxForecast[r].Rainfall;
    if (Units == "I") snow_readings[r]     = WxForecast[r].Snowfall * 0.0393701; else snow_readings[r]     = WxForecast[r].Snowfall;
    temperature_readings[r]                = WxForecast[r].Temperature;
    humidity_readings[r]                   = WxForecast[r].Humidity;
    r++;
  } while (r < max_readings);
  int gwidth = 175, gheight = 100;
  int gx = (SCREEN_WIDTH - gwidth * 4) / 5 + 8;
  int gy = (SCREEN_HEIGHT - gheight - 30);
  int gap = gwidth + gx;
  // (x,y,width,height,MinValue, MaxValue, Title, Data Array, AutoScale, ChartMode)
  DrawGraph(gx + 0 * gap, gy, gwidth, gheight, 900, 1050, Units == "M" ? TXT_PRESSURE_HPA : TXT_PRESSURE_IN, pressure_readings, max_readings, autoscale_on, barchart_off, 5, false);
  DrawGraph(gx + 1 * gap, gy, gwidth, gheight, 10, 30,    Units == "M" ? TXT_TEMPERATURE_C : TXT_TEMPERATURE_F, temperature_readings, max_readings, autoscale_on, barchart_off, 5, false);
  DrawGraph(gx + 2 * gap, gy, gwidth, gheight, 0, 100,   TXT_HUMIDITY_PERCENT, humidity_readings, max_readings, autoscale_off, barchart_off, 5, false);
  if (SumOfPrecip(rain_readings, max_readings) >= SumOfPrecip(snow_readings, max_readings))
    DrawGraph(gx + 3 * gap + 5, gy, gwidth, gheight, 0, 30, Units == "M" ? TXT_RAINFALL_MM : TXT_RAINFALL_IN, rain_readings, max_readings, autoscale_on, barchart_on, 5, false);
  else
    DrawGraph(gx + 3 * gap + 5, gy, gwidth, gheight, 0, 30, Units == "M" ? TXT_SNOWFALL_MM : TXT_SNOWFALL_IN, snow_readings, max_readings, autoscale_on, barchart_on, 5, false);
}

void DisplayConditionsSection(int x, int y, String IconName, bool IconSize) {
  if      (IconName == "01d" || IconName == "01n") ClearSky(x, y, IconSize, IconName);
  else if (IconName == "02d" || IconName == "02n") FewClouds(x, y, IconSize, IconName);
  else if (IconName == "03d" || IconName == "03n") ScatteredClouds(x, y, IconSize, IconName);
  else if (IconName == "04d" || IconName == "04n") BrokenClouds(x, y, IconSize, IconName);
  else if (IconName == "09d" || IconName == "09n") ChanceRain(x, y, IconSize, IconName);
  else if (IconName == "10d" || IconName == "10n") Rain(x, y, IconSize, IconName);
  else if (IconName == "11d" || IconName == "11n") Thunderstorms(x, y, IconSize, IconName);
  else if (IconName == "13d" || IconName == "13n") Snow(x, y, IconSize, IconName);
  else if (IconName == "50d" || IconName == "50n") Mist(x, y, IconSize, IconName);
  else                                             Nodata(x, y, IconSize, IconName);
}

void arrow(int x, int y, int asize, float aangle, int pwidth, int plength) {
  float dx = (asize - 10) * cos((aangle - 90) * PI / 180) + x; // calculate X position
  float dy = (asize - 10) * sin((aangle - 90) * PI / 180) + y; // calculate Y position
  float x1 = 0;         float y1 = plength;
  float x2 = pwidth / 2;  float y2 = pwidth / 2;
  float x3 = -pwidth / 2; float y3 = pwidth / 2;
  float angle = aangle * PI / 180 - 135;
  float xx1 = x1 * cos(angle) - y1 * sin(angle) + dx;
  float yy1 = y1 * cos(angle) + x1 * sin(angle) + dy;
  float xx2 = x2 * cos(angle) - y2 * sin(angle) + dx;
  float yy2 = y2 * cos(angle) + x2 * sin(angle) + dy;
  float xx3 = x3 * cos(angle) - y3 * sin(angle) + dx;
  float yy3 = y3 * cos(angle) + x3 * sin(angle) + dy;
  fillTriangle(xx1, yy1, xx3, yy3, xx2, yy2, Black);
}

void DrawSegment(int x, int y, int o1, int o2, int o3, int o4, int o11, int o12, int o13, int o14) {
  drawLine(x + o1,  y + o2,  x + o3,  y + o4,  Black);
  drawLine(x + o11, y + o12, x + o13, y + o14, Black);
}

void DrawPressureAndTrend(int x, int y, float pressure, String slope) {
  drawString(x + 25, y - 10, String(pressure, (Units == "M" ? 0 : 1)) + (Units == "M" ? "hPa" : "in"), LEFT);
  if      (slope == "+") {
    DrawSegment(x, y, 0, 0, 8, -8, 8, -8, 16, 0);
    DrawSegment(x - 1, y, 0, 0, 8, -8, 8, -8, 16, 0);
  }
  else if (slope == "0") {
    DrawSegment(x, y, 8, -8, 16, 0, 8, 8, 16, 0);
    DrawSegment(x - 1, y, 8, -8, 16, 0, 8, 8, 16, 0);
  }
  else if (slope == "-") {
    DrawSegment(x, y, 0, 0, 8, 8, 8, 8, 16, 0);
    DrawSegment(x - 1, y, 0, 0, 8, 8, 8, 8, 16, 0);
  }
}

void DisplayStatusSection(int x, int y, int rssi) {
  setFont(OpenSans8B);
  DrawRSSI(x + 305, y + 15, rssi);
  DrawBattery(x + 150, y);
}

void DrawRSSI(int x, int y, int rssi) {
  int WIFIsignal = 0;
  int xpos = 1;
  for (int _rssi = -100; _rssi <= rssi; _rssi = _rssi + 20) {
    if (_rssi <= -20)  WIFIsignal = 30; //            <-20dbm displays 5-bars
    if (_rssi <= -40)  WIFIsignal = 24; //  -40dbm to  -21dbm displays 4-bars
    if (_rssi <= -60)  WIFIsignal = 18; //  -60dbm to  -41dbm displays 3-bars
    if (_rssi <= -80)  WIFIsignal = 12; //  -80dbm to  -61dbm displays 2-bars
    if (_rssi <= -100) WIFIsignal = 6;  // -100dbm to  -81dbm displays 1-bar
    fillRect(x + xpos * 8, y - WIFIsignal, 6, WIFIsignal, Black);
    xpos++;
  }
}

void GetHighsandLows() {
  for (int d = 0; d < max_readings; d++) {
    HLReadings[d].Time = "";
    HLReadings[d].High = (Units == "M"?-50:-58);
    HLReadings[d].Low  = (Units == "M"?70:158);
  }
  int Day = 0;
  String StartTime  = "00:00" + String((Units == "M"?"":"A"));
  String FinishTime = "02:00" + String((Units == "M"?"":"A"));
  for (int r = 0; r < max_readings; r++) {
    if (ConvertUnixTime(WxForecast[r].Dt).substring(0, (Units == "M"?5:6)) >= StartTime && ConvertUnixTime(WxForecast[r].Dt).substring(0, (Units == "M"?5:6)) <= FinishTime) { // found first period in day
      HLReadings[Day].Time = ConvertUnixTime(WxForecast[r].Dt).substring(0, (Units == "M"?5:6));
      for (int InDay = 0; InDay < 8; InDay++) { // 00:00 to 21:00 is 8 readings
        if (r + InDay < max_readings) {
          if (WxForecast[r + InDay].High > HLReadings[Day].High) {
            HLReadings[Day].High = WxForecast[r + InDay].High;
          }
          if (WxForecast[r + InDay].Low  < HLReadings[Day].Low)  {
            HLReadings[Day].Low  = WxForecast[r + InDay].Low;
          }
        }
      }
      Day++;
    }
  }
} // Now the array HLReadings has 5-days of Highs and Lows

String ConvertUnixTime(int unix_time) {
  // Returns either '21:12  ' or ' 09:12pm' depending on Units mode
  time_t tm = unix_time;
  struct tm *now_tm = localtime(&tm);
  char output[40], FDay[40];
  if (Units == "M") {
    strftime(output, sizeof(output), "%H:%M %d/%m/%y", now_tm);
    strftime(FDay, sizeof(FDay), "%w", now_tm);
  }
  else {
    strftime(output, sizeof(output), "%I:%M%p %m/%d/%y", now_tm);
    strftime(FDay, sizeof(FDay), "%w", now_tm);
  }
  ForecastDay = weekday_D[String(FDay).toInt()];
  return output;
}

void DrawBattery(int x, int y) {
  uint8_t percentage = 100;
  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
    vref = adc_chars.vref;
  }
  float voltage = analogRead(36) / 4096.0 * 6.566 * (vref / 1000.0);
  if (voltage > 1 ) { // Only display if there is a valid reading
    Serial.println("\nVoltage = " + String(voltage));
    percentage = 2836.9625 * pow(voltage, 4) - 43987.4889 * pow(voltage, 3) + 255233.8134 * pow(voltage, 2) - 656689.7123 * voltage + 632041.7303;
    if (voltage >= 4.20) percentage = 100;
    if (voltage <= 3.20) percentage = 0;  // orig 3.5
    drawRect(x + 25, y - 16, 40, 15, Black);
    fillRect(x + 65, y - 12, 4, 7, Black);
    fillRect(x + 27, y - 14, 36 * percentage / 100.0, 11, Black);
    drawString(x + 85, y - 14, String(percentage) + "%  " + String(voltage, 1) + "v", LEFT);
  }
}

// Symbols are drawn on a relative 10x10grid and 1 scale unit = 1 drawing unit
void addcloud(int x, int y, int scale, int linesize) {
  fillCircle(x - scale * 3, y, scale, Black);                                                              // Left most circle
  fillCircle(x + scale * 3, y, scale, Black);                                                              // Right most circle
  fillCircle(x - scale, y - scale, scale * 1.4, Black);                                                    // left middle upper circle
  fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75, Black);                                       // Right middle upper circle
  fillRect(x - scale * 3 - 1, y - scale, scale * 6, scale * 2 + 1, Black);                                 // Upper and lower lines
  fillCircle(x - scale * 3, y, scale - linesize, White);                                                   // Clear left most circle
  fillCircle(x + scale * 3, y, scale - linesize, White);                                                   // Clear right most circle
  fillCircle(x - scale, y - scale, scale * 1.4 - linesize, White);                                         // left middle upper circle
  fillCircle(x + scale * 1.5, y - scale * 1.3, scale * 1.75 - linesize, White);                            // Right middle upper circle
  fillRect(x - scale * 3 + 2, y - scale + linesize - 1, scale * 5.9, scale * 2 - linesize * 2 + 2, White); // Upper and lower lines
}

void addrain(int x, int y, int scale, bool IconSize) {
  if (IconSize == SmallIcon) {
    setFont(OpenSans8B);
    drawString(x - 25, y + 12, "///////", LEFT);
  }
  else
  {
    setFont(OpenSans18B);
    drawString(x - 60, y + 25, "///////", LEFT);
  }
}

void addsnow(int x, int y, int scale, bool IconSize) {
  if (IconSize == SmallIcon) {
    setFont(OpenSans8B);
    drawString(x - 25, y + 15, "* * * *", LEFT);
  }
  else
  {
    setFont(OpenSans18B);
    drawString(x - 60, y + 30, "* * * *", LEFT);
  }
}

void addtstorm(int x, int y, int scale) {
  y = y + scale / 2;
  for (int i = 1; i < 5; i++) {
    drawLine(x - scale * 4 + scale * i * 1.5 + 0, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 0, y + scale, Black);
    if (scale != Small) {
      drawLine(x - scale * 4 + scale * i * 1.5 + 1, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 1, y + scale, Black);
      drawLine(x - scale * 4 + scale * i * 1.5 + 2, y + scale * 1.5, x - scale * 3.5 + scale * i * 1.5 + 2, y + scale, Black);
    }
    drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 0, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 0, Black);
    if (scale != Small) {
      drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 1, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 1, Black);
      drawLine(x - scale * 4 + scale * i * 1.5, y + scale * 1.5 + 2, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5 + 2, Black);
    }
    drawLine(x - scale * 3.5 + scale * i * 1.4 + 0, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 0, y + scale * 1.5, Black);
    if (scale != Small) {
      drawLine(x - scale * 3.5 + scale * i * 1.4 + 1, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 1, y + scale * 1.5, Black);
      drawLine(x - scale * 3.5 + scale * i * 1.4 + 2, y + scale * 2.5, x - scale * 3 + scale * i * 1.5 + 2, y + scale * 1.5, Black);
    }
  }
}

void addsun(int x, int y, int scale, bool IconSize) {
  int linesize = 5;
  fillRect(x - scale * 2, y, scale * 4, linesize, Black);
  fillRect(x, y - scale * 2, linesize, scale * 4, Black);
  DrawAngledLine(x + scale * 1.4, y + scale * 1.4, (x - scale * 1.4), (y - scale * 1.4), linesize, Black); // Actually sqrt(2) but 1.4 is good enough
  DrawAngledLine(x - scale * 1.4, y + scale * 1.4, (x + scale * 1.4), (y - scale * 1.4), linesize, Black);
  fillCircle(x, y, scale * 1.3, White);
  fillCircle(x, y, scale, Black);
  fillCircle(x, y, scale - linesize, White);
}

void addfog(int x, int y, int scale, int linesize, bool IconSize) {
  if (IconSize == SmallIcon) linesize = 3;
  for (int i = 0; i < 6; i++) {
    fillRect(x - scale * 3, y + scale * 1.5, scale * 6, linesize, Black);
    fillRect(x - scale * 3, y + scale * 2.0, scale * 6, linesize, Black);
    fillRect(x - scale * 3, y + scale * 2.5, scale * 6, linesize, Black);
  }
}

void DrawAngledLine(int x, int y, int x1, int y1, int size, int color) {
  int dx = (size / 2.0) * (x - x1) / sqrt(sq(x - x1) + sq(y - y1));
  int dy = (size / 2.0) * (y - y1) / sqrt(sq(x - x1) + sq(y - y1));
  fillTriangle(x + dx, y - dy, x - dx,  y + dy,  x1 + dx, y1 - dy, color);
  fillTriangle(x - dx, y + dy, x1 - dx, y1 + dy, x1 + dx, y1 - dy, color);
}

void ClearSky(int x, int y, bool IconSize, String IconName) {
  int scale = Small;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  else {
    y -= 3; // Shift up small sun icon
    scale *= 0.8;
  }
  addsun(x, y, scale * 1.6, IconSize);
}

void BrokenClouds(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  y += 15;
  if (IconSize == LargeIcon) scale = Large;
  addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
  addcloud(x, y, scale * (IconSize ? 1 : 0.75), linesize);
}

void MostlyCloudy(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  addcloud(x, y, scale, linesize);
  addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
}

void FewClouds(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  y += 15;
  if (IconSize == LargeIcon) scale = Large;
  addcloud(x + (IconSize ? 10 : 0), y, scale * (IconSize ? 0.9 : 0.8), linesize);
  addsun((x + (IconSize ? 10 : 0)) - scale * 1.8, y - scale * 1.6, scale, IconSize);
}

void ScatteredClouds(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  y += 15;
  if (IconSize == LargeIcon) scale = Large;
  addcloud(x - (IconSize ? 35 : 0), y * (IconSize ? 0.75 : 0.93), scale / 2, linesize); // Cloud top left
  addcloud(x, y, scale * 0.9, linesize);                                                // Main cloud
}

void Rain(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  else {
    scale *= 0.7;
    y += 8;
  }
  addcloud(x, y, scale, linesize);
  addrain(x, y, scale, IconSize);
}

void ExpectRain(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
  addcloud(x, y, scale, linesize);
  addrain(x, y, scale, IconSize);
}

void ChanceRain(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  addsun(x - scale * 1.8, y - scale * 1.8, scale, IconSize);
  addcloud(x, y, scale * (IconSize ? 1 : 0.75), linesize);
  addrain(x, y, scale, IconSize);
}

void Thunderstorms(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  addcloud(x, y, scale * (IconSize ? 1 : 0.75), linesize);
  addtstorm(x, y, scale);
}

void Snow(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  addcloud(x, y, scale * (IconSize ? 1 : 0.75), linesize);
  addsnow(x, y, scale, IconSize);
}

void Mist(int x, int y, bool IconSize, String IconName) {
  int scale = Small, linesize = 5;
  if (IconName.endsWith("n")) addmoon(x, y, IconSize);
  if (IconSize == LargeIcon) scale = Large;
  addcloud(x, y, scale * (IconSize ? 1 : 0.75), linesize);
  addfog(x, y, scale, linesize, IconSize);
}

void CloudCover(int x, int y, int CloudCover) {
  addcloud(x - 9, y,     Small * 0.3, 2); // Cloud top left
  addcloud(x + 3, y - 2, Small * 0.3, 2); // Cloud top right
  addcloud(x, y + 15,    Small * 0.6, 2); // Main cloud
  drawString(x + 30, y, String(CloudCover) + "%", LEFT);
}

void Visibility(int x, int y, String Visibility) {
  float start_angle = 0.52, end_angle = 2.61, Offset = 10;
  int r = 14;
  for (float i = start_angle; i < end_angle; i = i + 0.05) {
    drawPixel(x + r * cos(i), y - r / 2 + r * sin(i) + Offset, Black);
    drawPixel(x + r * cos(i), 1 + y - r / 2 + r * sin(i) + Offset, Black);
  }
  start_angle = 3.61; end_angle = 5.78;
  for (float i = start_angle; i < end_angle; i = i + 0.05) {
    drawPixel(x + r * cos(i), y + r / 2 + r * sin(i) + Offset, Black);
    drawPixel(x + r * cos(i), 1 + y + r / 2 + r * sin(i) + Offset, Black);
  }
  fillCircle(x, y + Offset, r / 4, Black);
  drawString(x + 20, y, Visibility, LEFT);
}

void addmoon(int x, int y, bool IconSize) {
  int xOffset = 65;
  int yOffset = 12;
  if (IconSize == LargeIcon) {
    xOffset = 130;
    yOffset = -40;
  }
  fillCircle(x - 28 + xOffset, y - 37 + yOffset, uint16_t(Small * 1.0), Black);
  fillCircle(x - 16 + xOffset, y - 37 + yOffset, uint16_t(Small * 1.6), White);
}

void Nodata(int x, int y, bool IconSize, String IconName) {
  if (IconSize == LargeIcon) setFont(OpenSans24B); else setFont(OpenSans12B);
  drawString(x - 3, y - 10, "?", CENTER);
}

void DrawMoonImage(int x, int y) {
  Rect_t area = {
    .x = x, .y = y, .width  = moon_width, .height =  moon_height
  };
  epd_draw_grayscale_image(area, (uint8_t *) moon_data);
}

void DrawSunriseImage(int x, int y) {
  Rect_t area = {
    .x = x, .y = y, .width  = sunrise_width, .height =  sunrise_height
  };
  drawImage(area, (uint8_t *) sunrise_data);
}

void DrawSunsetImage(int x, int y) {
  Rect_t area = {
    .x = x, .y = y, .width  = sunset_width, .height =  sunset_height
  };
  drawImage(area, (uint8_t *) sunset_data);
}

void DrawUVI(int x, int y) {
  Rect_t area = {
    .x = x, .y = y, .width  = uvi_width, .height = uvi_height
  };
  drawImage(area, (uint8_t *) uvi_data);
}

/* (C) D L BIRD
    This function will draw a graph on a ePaper/TFT/LCD display using data from an array containing data to be graphed.
    The variable 'max_readings' determines the maximum number of data elements for each array. Call it with the following parametric data:
    x_pos-the x axis top-left position of the graph
    y_pos-the y-axis top-left position of the graph, e.g. 100, 200 would draw the graph 100 pixels along and 200 pixels down from the top-left of the screen
    width-the width of the graph in pixels
    height-height of the graph in pixels
    Y1_Max-sets the scale of plotted data, for example 5000 would scale all data to a Y-axis of 5000 maximum
    data_array1 is parsed by value, externally they can be called anything else, e.g. within the routine it is called data_array1, but externally could be temperature_readings
    auto_scale-a logical value (TRUE or FALSE) that switches the Y-axis autoscale On or Off
    barchart_on-a logical value (TRUE or FALSE) that switches the drawing mode between barhcart and line graph
    barchart_colour-a sets the title and graph plotting colour
    days amount of days the graph should cover, used to label the graph
    past determines if the graph shows past or future data
    If called with Y!_Max value of 500 and the data never goes above 500, then autoscale will retain a 0-500 Y scale, if on, the scale increases/decreases to match the data.
    auto_scale_margin, e.g. if set to 1000 then autoscale increments the scale by 1000 steps.
*/
void DrawGraph(int x_pos, int y_pos, int gwidth, int gheight, float Y1Min, float Y1Max, String title, float DataArray[], int readings, boolean auto_scale, boolean barchart_mode, int days, boolean past) {
#define auto_scale_margin 0 // Sets the autoscale increment, so axis steps up after a change of e.g. 3
#define y_minor_axis 5      // 5 y-axis division markers
  setFont(OpenSans10B);
  int maxYscale = -10000;
  int minYscale =  10000;
  int last_x, last_y;
  float x2, y2;
  if (auto_scale == true) {
    for (int i = 1; i < readings; i++ ) {
      if (DataArray[i] >= maxYscale) maxYscale = DataArray[i];
      if (DataArray[i] <= minYscale) minYscale = DataArray[i];
    }
    maxYscale = round(maxYscale + auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Max
    Y1Max = round(maxYscale + 0.5);
    if (minYscale != 0) minYscale = round(minYscale - auto_scale_margin); // Auto scale the graph and round to the nearest value defined, default was Y1Min
    Y1Min = round(minYscale);
  }
  // Draw the graph
  last_x = x_pos + 1;
  last_y = y_pos + (Y1Max - constrain(DataArray[1], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight;
  drawRect(x_pos, y_pos, gwidth + 3, gheight + 2, Grey);
  drawString(x_pos - 20 + gwidth / 2, y_pos - 28, title, CENTER);
  for (int gx = 0; gx < readings; gx++) {
    x2 = x_pos + gx * gwidth / (readings - 1) - 1 ; // max_readings is the global variable that sets the maximum data that can be plotted
    y2 = y_pos + (Y1Max - constrain(DataArray[gx], Y1Min, Y1Max)) / (Y1Max - Y1Min) * gheight + 1;
    if (barchart_mode) {
      fillRect(last_x + 2, y2, (gwidth / readings) - 1, y_pos + gheight - y2 + 2, Black);
    } else {
      drawLine(last_x, last_y - 1, x2, y2 - 1, Black); // two lines for hi-res display
      drawLine(last_x, last_y, x2, y2, Black);
    }
    last_x = x2;
    last_y = y2;
  }
  //Draw the Y-axis scale
#define number_of_dashes 20
  for (int spacing = 0; spacing <= y_minor_axis; spacing++) {
    for (int j = 0; j < number_of_dashes; j++) { // draw dashed graph grid lines
      if (spacing < y_minor_axis) drawFastHLine((x_pos + 3 + j * gwidth / number_of_dashes), y_pos + (gheight * spacing / y_minor_axis), gwidth / (2 * number_of_dashes), Grey);
    }
    if ((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing) < 5 || title == TXT_PRESSURE_IN) {
      drawString(x_pos - 10, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
    }
    else
    {
      if (Y1Min < 1 && Y1Max < 10) {
        drawString(x_pos - 3, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 1), RIGHT);
      }
      else {
        drawString(x_pos - 7, y_pos + gheight * spacing / y_minor_axis - 5, String((Y1Max - (float)(Y1Max - Y1Min) / y_minor_axis * spacing + 0.01), 0), RIGHT);
      }
    }
  }
  for (int d = 0; d < days; d++) {
    int day = d + 1;
    if (past) day = (days - d) * -1;
    drawString(days + x_pos + gwidth / days * d, y_pos + gheight + 10, String(day) + "d", LEFT);
    if (d < days) drawFastVLine(x_pos + gwidth / days * d + gwidth / days, y_pos, gheight, LightGrey);
  }
}

void drawString(int x, int y, String text, alignment align) {
  char * data  = const_cast<char*>(text.c_str());
  int  x1, y1; // the bounds of x,y and w and h of the variable 'text' in pixels.
  int w, h;
  int xx = x, yy = y;
  get_text_bounds(&currentFont, data, &xx, &yy, &x1, &y1, &w, &h, NULL);
  if (align == RIGHT)  x = x - w;
  if (align == CENTER) x = x - w / 2;
  int cursor_y = y + h;
  write_string(&currentFont, data, &x, &cursor_y, framebuffer);
}

void fillCircle(int x, int y, int r, uint8_t color) {
  epd_fill_circle(x, y, r, color, framebuffer);
}

void drawFastHLine(int16_t x0, int16_t y0, int length, uint16_t color) {
  epd_draw_hline(x0, y0, length, color, framebuffer);
}

void drawFastVLine(int16_t x0, int16_t y0, int length, uint16_t color) {
  epd_draw_vline(x0, y0, length, color, framebuffer);
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  epd_write_line(x0, y0, x1, y1, color, framebuffer);
}

void drawCircle(int x0, int y0, int r, uint8_t color) {
  epd_draw_circle(x0, y0, r, color, framebuffer);
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  epd_draw_rect(x, y, w, h, color, framebuffer);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  epd_fill_rect(x, y, w, h, color, framebuffer);
}

void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color) {
  epd_fill_triangle(x0, y0, x1, y1, x2, y2, color, framebuffer);
}

void drawPixel(int x, int y, uint8_t color) {
  epd_draw_pixel(x, y, color, framebuffer);
}

void drawImage(Rect_t area, uint8_t* data) {
  epd_copy_to_framebuffer(area, data, framebuffer);
}

void setFont(GFXfont const & font) {
  currentFont = font;
}
