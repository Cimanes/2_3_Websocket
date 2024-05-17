// =============================================
// OPTIONS 
// =============================================
#define useButton       // Use buttons
#define useBVAR         // Use Boolean control variables
#define useToggle       // Use toggle switches (ON - OFF)
#define usePWM          // Use analog output channels (PWM's)
#define useAVAR         // Use floating control variables
#define useBME          // Use BME sensor
#define debug           // for debugging purpose only. Remove for final version.
#define apartment       // OPTIONAL: Choose Wifi credentials [Cimanes, Toledo, apartment]
const int aFactor = 10; // Factor for range of analog signals (10 -> one decimal; 100 -> 2 decimals). Match with JS!
#define connectI2C      // Use I2C communication (for BME sensor)

#if defined(Cimanes)
  const char ssid[] = "Pepe_Cimanes";
  const char pass[] = "Cimanes7581" ;
#elif defined(Toledo)
  const char ssid[] = "MIWIFI_HtR7" ;
  const char pass[] = "TdQTDf3H"    ;
#elif defined(apartment)
  const char ssid[] = "HH71VM_309B_2.4G" ;
  const char pass[] = "Ey6DKct3"    ;
#endif

// =============================================
// LIBRARIES
// =============================================
#include <Arduino.h>
#include <SimpleTimer.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_JSON.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#ifdef useBME
  #include <Adafruit_BME280.h>
  #include <Adafruit_Sensor.h>
#endif
// #define ESP8266            // OPTIONAL: (defined in platformIO.ini). Board family: [ESP8266, ESP32]
#if defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
  #include "SPIFFS.h"         // OPTIONAL: available for SPIFFS in ESP32 only
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
  #include <LittleFS.h>       // OPTIONAL: Little file system for ESP8266
#endif


// =============================================
// PIN DEFINITIONS 
// =============================================
#if defined(ESP32)
  #ifdef connectI2C     // Config. I2C connections (SDA and SCL for BME sensor)
    #define sdaPin 21
    #define sclPin 22
  #endif
  #ifdef useButton                       // Config. D.O. GPIO's used by buttons (ON/OFF, AUTO/MAN)
    #define statePin 32
    #define modePin 33
  #endif
  #ifdef useToggle                       // Config. toggle switches
    #define numDOs 2                     // Number of toggle switches
    const byte arrDO[numDOs] = {34, 35}; // D.O. GPIO's used by toggle switches
  #endif
  #ifdef usePWM                          // Config. PWM analog outputs
    #define numPWMs 2                    // Number of toggle switches
    // Config PWM. Format "{channel, rangeMin, rangeMax}"
    const int arrPWM[numPWMs][4] = {{16, 0, 1000}, {17, 50, 350}};
    int PWMval[numPWMs] = {0, 0};   // Store and report PWM values
  #endif
#elif defined(ESP8266)
  #ifdef connectI2C                      // Config. I2C connections (SDA and SCL for BME sensor)
    #define sdaPin 4
    #define sclPin 5
  #endif
  #ifdef useButton                       // Config. D.O. GPIO's used by buttons (ON/OFF, AUTO/MAN)
    #define statePin 0
    #define modePin 2
  #endif
  #ifdef useToggle                       // Config. toggle switches
    #define numDOs 2                     // Number of toggle switches
    const byte arrDO[numDOs] = {12, 14}; // D.O. GPIO's used by toggle switches
  #endif
  #ifdef usePWM                          // Config. PWM analog outputs
    #define numPWMs 2                    // Number of toggle switches
    // Config PWM. Format "{channel, rangeMin, rangeMax}" (match ranges with JS and )
    const int arrPWM[numPWMs][4] = {{13, 0, 1000}, {15, 0, 2000}};
    int PWMval[numPWMs] = {0, 0};   // Store and report PWM values
  #endif
#endif

// =============================================
// GLOBAL VARIABLES 
// =============================================
// Configure feedback messages for console and BME sensor (Websocket and SSE):
const byte fbkLength = 60;        // Max length of feedback message
char feedbackChar[fbkLength];     // Char array to store the JSON object

// Config. boolean variables
#ifdef useBVAR                                      
  #define numBVARS 2                                // Number of boolean variables
  const char* BVAR[numBVARS] = {"bVAR1", "bVAR2"};  // Boolean variable names 
  bool BVARval[numBVARS] = {0, 0};                  // Store boolean variable initial values
#endif

// Config. analog variables (match initial value with JS range)
#ifdef useAVAR                                        
  #define numAVARS 2                                // Number of analog variables
  const char* AVAR[numAVARS] = {"tSET", "rhSET"};   // Analog variable names 
  int AVARval[numAVARS] = {50, 0};                   // Analog variable initial values  
#endif

#ifdef useBME
  // Create BME sensor object; connect to ESP I2C (GPIO 21 = SDA, GPIO 22 = SCL)
  Adafruit_BME280 bme;
  
  // Define NTP Client to get time (used in data file)
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP, "pool.ntp.org");  
  
  // File name where readings will be saved, and maximum size (bytes)
  const char* dataPath = "/data.txt";
  const int fileLength = 4000U;

  // Function to initialize BME280 sensor
  void initBME(){
    if (!bme.begin(0x76)) { Serial.println(F("BME280 not Found")); while (1);}
    else Serial.println(F("BME280 found"));
  }
#endif

// =============================================
// MANAGE FILE SYSTEM
// =============================================
void initFS() {
  if (!LittleFS.begin()) Serial.println(F("Error mounting File System"));
  // if (!SPIFFS.begin(true)) Serial.println("Error mounting File System");      // particular for SPIFFS in ESP32 only
  else Serial.println(F("File System mounted"));
}

// =============================================
// MANAGE COMMUNICATIONS 
// =============================================
// Function to Initialize Wifi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print(F("Connecting to WiFi .."));
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

// Create AsyncWebServer object on port 80, a WebSocket object ("/ws") and an Event Source ("/events"):
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");             // Required for WEBSOCKETS
AsyncEventSource events("/events");   // Required for SERVER SENT EVENTS

// Setup timers for periodic tasks (websocket clean and take BME readings):
SimpleTimer timer;
const int cleanTimer = 2000UL;
#ifdef useBME
  const int BMETimer = 30000UL;
#endif

