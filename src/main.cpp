/*  SBM-20 based Geiger Counter
    Author: Prabhat    Email: pra22@pitt.edu
    Sketch for ESP8266 that counts clicks from the Geiger tube, calculates the counts per minute, and displays information 
    on a TFT touchscreen.
    Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)
*/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EthernetClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WifiManager.h>
#include <EEPROM.h>
#include "SPI.h"
#include "Adafruit_GFX.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "Adafruit_ILI9341.h"
#include <XPT2046_Touchscreen.h>

#define CS_PIN D2
XPT2046_Touchscreen ts(CS_PIN);

#define TS_MINX 250
#define TS_MINY 200 // calibration points for touchscreen
#define TS_MAXX 3800
#define TS_MAXY 3750

#define TFT_DC D4
#define TFT_CS D8

#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF
#define DOSEBACKGROUND 0x0455

// WiFi variables
unsigned long currentUploadTime;
unsigned long previousUploadTime;
int passwordLength;
int SSIDLength;
int channelIDLength;
int writeAPILength;
char ssid[20];
char password[20];
char channelID[20]; // = "864288";
char channelAPIkey[20]; // = "37SAHQPEQ7FOBC20";
char server[] = "api.thingspeak.com";
int attempts; // number of connection attempts when device starts up in monitoring mode
WiFiClient client;

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

const int interruptPin = 5;

long count[61];
long fastCount[6]; // arrays to store running counts
long slowCount[181];
int i = 0;         // array elements
int j = 0;
int k = 0;

int page = 0;

long currentMillis;
long previousMillis;
unsigned long currentMicros;
unsigned long previousMicros;

unsigned long averageCount;
unsigned long currentCount;  // incremented by interrupt
unsigned long previousCount; // to activate buzzer and LED
unsigned long cumulativeCount;
float doseRate;
float totalDose;
char dose[5];
int doseLevel;               // determines home screen warning signs
int previousDoseLevel;

bool ledSwitch = 1;
bool buzzerSwitch = 1;
bool wasTouched;
int integrationMode = 0; // 0 = medium, 1 = fast, 2 == slow;

bool doseUnits = 0; // 0 = Sievert, 1 = Rem
unsigned int alarmThreshold = 5;
unsigned int conversionFactor = 175;

int x, y; // touch points

// Battery indicator variables
int batteryInput;
int batteryPercent;
int batteryMapped = 212;       // pixel location of battery icon
int batteryUpdateCounter = 29;

// EEPROM variables
const int saveUnits = 0;
const int saveAlertThreshold = 1; // Addresses for storing settings data in the EEPROM
const int saveCalibration = 2;
const int saveDeviceMode = 3;
const int saveLoggingMode = 4;
const int saveSSIDLen = 5;
const int savePWLen = 6;
const int saveIDLen = 7;
const int saveAPILen = 8;

// Data Logging variables
int addr = 200;                 // starting address for data logging
char jsonBuffer[14000] = "["; 
char data[14500] = "{\"write_api_key\":\"";
unsigned long currentLogTime;
unsigned long previousLogTime;


// Timed Count Variables:
int interval = 5;
unsigned long intervalMillis;
unsigned long startMillis;
unsigned long elapsedTime;
int progress;
float cpm;
bool completed = 0;
int intervalSize; // stores how many digits are in the interval

// Logging variables
bool isLogging;

bool deviceMode;

// interrupt routine declaration
void ICACHE_RAM_ATTR isr();

unsigned int previousIntMicros;              // timers to limit count increment rate in the ISR

const unsigned char gammaBitmap [] PROGMEM = {
	0x30, 0x00, 0x78, 0x70, 0xe8, 0xe0, 0xc4, 0xe0, 0x84, 0xc0, 0x05, 0xc0, 0x05, 0x80, 0x07, 0x80, 
	0x03, 0x00, 0x07, 0x00, 0x0e, 0x00, 0x0e, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x1e, 0x00, 0x3e, 0x00, 
	0x1c, 0x00, 0x00, 0x00
};

const unsigned char betaBitmap [] PROGMEM = {
	0x00, 0xc0, 0x00, 0x03, 0xf0, 0x00, 0x07, 0x18, 0x00, 0x06, 0x18, 0x00, 0x0e, 0x18, 0x00, 0x0e, 
	0x18, 0x00, 0x0e, 0xf8, 0x00, 0x0e, 0x1c, 0x00, 0x0e, 0x0c, 0x00, 0x0e, 0x0c, 0x00, 0x0e, 0x0c, 
	0x00, 0x0e, 0x0c, 0x00, 0x0f, 0x1c, 0x00, 0x0f, 0xf8, 0x00, 0x0e, 0x00, 0x00, 0x0e, 0x00, 0x00, 
	0x0c, 0x00, 0x00, 0x00, 0x00, 0x00
};
const unsigned char wifiBitmap [] PROGMEM = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xf0, 0x00, 0x0f, 0xfe, 0x00, 0x3f, 0xff, 0x80, 0x78, 
	0x03, 0xc0, 0xe0, 0x00, 0xe0, 0x47, 0xfc, 0x40, 0x0f, 0xfe, 0x00, 0x1c, 0x07, 0x00, 0x08, 0x02, 
	0x00, 0x01, 0xf0, 0x00, 0x03, 0xf8, 0x00, 0x01, 0x10, 0x00, 0x00, 0x40, 0x00, 0x00, 0xe0, 0x00, 
	0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const unsigned char settingsBitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00,
    0x00, 0x01, 0xc0, 0x7f, 0xe0, 0x38, 0x00, 0x00, 0x00, 0x03, 0xf0, 0x7f, 0xe0, 0xfc, 0x00, 0x00,
    0x00, 0x07, 0xf9, 0xff, 0xf9, 0xfe, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x07, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00,
    0x00, 0x03, 0xff, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00,
    0x00, 0x01, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x03, 0xff, 0xf0, 0x7f, 0xfc, 0x00, 0x00,
    0x00, 0x03, 0xff, 0xc0, 0x3f, 0xfc, 0x00, 0x00, 0x00, 0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 0x00,
    0x00, 0xff, 0xff, 0x00, 0x0f, 0xff, 0xf0, 0x00, 0x01, 0xff, 0xff, 0x00, 0x07, 0xff, 0xf8, 0x00,
    0x01, 0xff, 0xfe, 0x00, 0x07, 0xff, 0xf8, 0x00, 0x01, 0xff, 0xfe, 0x00, 0x07, 0xff, 0xf8, 0x00,
    0x01, 0xff, 0xfe, 0x00, 0x07, 0xff, 0xf8, 0x00, 0x01, 0xff, 0xfe, 0x00, 0x07, 0xff, 0xf8, 0x00,
    0x01, 0xff, 0xfe, 0x00, 0x07, 0xff, 0xf8, 0x00, 0x00, 0xff, 0xff, 0x00, 0x0f, 0xff, 0xf0, 0x00,
    0x00, 0x1f, 0xff, 0x80, 0x1f, 0xff, 0x80, 0x00, 0x00, 0x03, 0xff, 0xc0, 0x3f, 0xfc, 0x00, 0x00,
    0x00, 0x03, 0xff, 0xe0, 0x7f, 0xfc, 0x00, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00,
    0x00, 0x01, 0xff, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x03, 0xff, 0xff, 0xff, 0xfc, 0x00, 0x00,
    0x00, 0x07, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00,
    0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x07, 0xf9, 0xff, 0xf9, 0xfe, 0x00, 0x00,
    0x00, 0x03, 0xf0, 0x7f, 0xe0, 0xfc, 0x00, 0x00, 0x00, 0x01, 0xc0, 0x7f, 0xe0, 0x38, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char buzzerOnBitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80, 0x0c, 0x00,
    0x00, 0x00, 0x07, 0x80, 0x0e, 0x00, 0x00, 0x00, 0x0f, 0x80, 0x0f, 0x00, 0x00, 0x00, 0x1f, 0x80,
    0x07, 0x00, 0x00, 0x00, 0x3f, 0x80, 0xc7, 0x80, 0x00, 0x00, 0xff, 0x80, 0xe3, 0x80, 0x00, 0x01,
    0xff, 0x80, 0xf3, 0xc0, 0x00, 0x03, 0xff, 0x80, 0x71, 0xc0, 0x00, 0x07, 0xff, 0x8c, 0x79, 0xc0,
    0x3f, 0xff, 0xff, 0x9e, 0x38, 0xe0, 0x3f, 0xff, 0xff, 0x8e, 0x38, 0xe0, 0x3f, 0xff, 0xff, 0x8e,
    0x3c, 0xe0, 0x3f, 0xff, 0xff, 0x87, 0x1c, 0xe0, 0x3f, 0xff, 0xff, 0x87, 0x1c, 0x60, 0x3f, 0xff,
    0xff, 0x87, 0x1c, 0x70, 0x3f, 0xff, 0xff, 0x87, 0x1c, 0x70, 0x3f, 0xff, 0xff, 0x87, 0x1c, 0x70,
    0x3f, 0xff, 0xff, 0x87, 0x1c, 0x70, 0x3f, 0xff, 0xff, 0x87, 0x1c, 0x70, 0x3f, 0xff, 0xff, 0x87,
    0x1c, 0xe0, 0x3f, 0xff, 0xff, 0x8e, 0x3c, 0xe0, 0x3f, 0xff, 0xff, 0x8e, 0x38, 0xe0, 0x3f, 0xff,
    0xff, 0x9e, 0x38, 0xe0, 0x00, 0x07, 0xff, 0x8c, 0x79, 0xc0, 0x00, 0x03, 0xff, 0x80, 0x71, 0xc0,
    0x00, 0x00, 0xff, 0x80, 0xf1, 0xc0, 0x00, 0x00, 0x7f, 0x80, 0xe3, 0x80, 0x00, 0x00, 0x3f, 0x80,
    0xc7, 0x80, 0x00, 0x00, 0x1f, 0x80, 0x07, 0x00, 0x00, 0x00, 0x0f, 0x80, 0x0f, 0x00, 0x00, 0x00,
    0x07, 0x80, 0x0e, 0x00, 0x00, 0x00, 0x03, 0x80, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char buzzerOffBitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x3f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x00, 0x00, 0x00, 0x00, 0xff, 0x80, 0x00, 0x00,
    0x00, 0x03, 0xff, 0x80, 0x00, 0x00, 0x00, 0x07, 0xff, 0x80, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x80,
    0x00, 0x00, 0x0f, 0xff, 0xff, 0x80, 0x00, 0x00, 0x1f, 0xff, 0xff, 0x80, 0x00, 0x00, 0x3f, 0xff,
    0xff, 0x8f, 0x00, 0x78, 0x7f, 0xff, 0xff, 0x8f, 0x80, 0xf8, 0x7f, 0xff, 0xff, 0x8f, 0xc1, 0xf8,
    0x7f, 0xff, 0xff, 0x87, 0xe3, 0xf0, 0x7f, 0xff, 0xff, 0x83, 0xf7, 0xe0, 0x7f, 0xff, 0xff, 0x81,
    0xff, 0xc0, 0x7f, 0xff, 0xff, 0x80, 0xff, 0x80, 0x7f, 0xff, 0xff, 0x80, 0x7f, 0x00, 0x7f, 0xff,
    0xff, 0x80, 0x7f, 0x00, 0x7f, 0xff, 0xff, 0x80, 0xff, 0x80, 0x7f, 0xff, 0xff, 0x81, 0xff, 0xc0,
    0x7f, 0xff, 0xff, 0x83, 0xf7, 0xe0, 0x7f, 0xff, 0xff, 0x87, 0xe3, 0xf0, 0x7f, 0xff, 0xff, 0x8f,
    0xc1, 0xf0, 0x7f, 0xff, 0xff, 0x8f, 0x80, 0xf8, 0x3f, 0xff, 0xff, 0x8f, 0x00, 0x70, 0x3f, 0xff,
    0xff, 0x84, 0x00, 0x20, 0x1f, 0xff, 0xff, 0x80, 0x00, 0x00, 0x0f, 0xff, 0xff, 0x80, 0x00, 0x00,
    0x00, 0x07, 0xff, 0x80, 0x00, 0x00, 0x00, 0x03, 0xff, 0x80, 0x00, 0x00, 0x00, 0x01, 0xff, 0x80,
    0x00, 0x00, 0x00, 0x00, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x00, 0x00, 0x00, 0x00,
    0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x03, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const unsigned char ledOnBitmap[] PROGMEM = {
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
    0x00, 0x00, 0x00, 0x18, 0x07, 0x00, 0xc0, 0x00, 0x00, 0x1c, 0x07, 0x01, 0xc0, 0x00, 0x00, 0x1e,
    0x07, 0x03, 0xc0, 0x00, 0x00, 0x0e, 0x07, 0x03, 0x80, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x80, 0x00,
    0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x1e, 0x00, 0x1f, 0xc0, 0x03, 0xc0, 0x0f, 0x80, 0x7f, 0xf0, 0x0f, 0x80, 0x07, 0xc1,
    0xff, 0xfc, 0x1f, 0x00, 0x03, 0xc3, 0xe0, 0x3e, 0x1e, 0x00, 0x00, 0x07, 0xc0, 0x0f, 0x00, 0x00,
    0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x80, 0x00, 0x00, 0x0e, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x80, 0x00, 0x00, 0x1c, 0x00, 0x01, 0xc0, 0x00, 0x7f, 0x1c,
    0x00, 0x01, 0xc3, 0xf0, 0x7f, 0x1c, 0x00, 0x01, 0xc7, 0xf0, 0x3c, 0x0e, 0x00, 0x03, 0x81, 0xe0,
    0x00, 0x0e, 0x00, 0x03, 0x80, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x80, 0x00, 0x00, 0x0f, 0x00, 0x07,
    0x80, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x07, 0x80, 0x0f, 0x00, 0x00, 0x01, 0xc3,
    0xc0, 0x1e, 0x1c, 0x00, 0x07, 0xc1, 0xc0, 0x1c, 0x1f, 0x00, 0x0f, 0x81, 0xe0, 0x3c, 0x0f, 0x80,
    0x1e, 0x00, 0xe0, 0x38, 0x03, 0xc0, 0x0c, 0x00, 0xe0, 0x38, 0x01, 0x80, 0x00, 0x00, 0xf0, 0x78,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00,
    0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00,
    0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0,
    0x00, 0x00, 0x00, 0x00, 0x1f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00};

const unsigned char ledOffBitmap[] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x1f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x01,
    0xff, 0xfc, 0x00, 0x00, 0x00, 0x03, 0xe0, 0x3e, 0x00, 0x00, 0x00, 0x07, 0xc0, 0x0f, 0x00, 0x00,
    0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x07, 0x80, 0x00, 0x00, 0x0e, 0x00, 0x03,
    0x80, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x80, 0x00, 0x00, 0x1c, 0x00, 0x01, 0xc0, 0x00, 0x00, 0x1c,
    0x00, 0x01, 0xc0, 0x00, 0x00, 0x1c, 0x00, 0x01, 0xc0, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x80, 0x00,
    0x00, 0x0e, 0x00, 0x03, 0x80, 0x00, 0x00, 0x0e, 0x00, 0x03, 0x80, 0x00, 0x00, 0x0f, 0x00, 0x07,
    0x80, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x07, 0x80, 0x0f, 0x00, 0x00, 0x00, 0x03,
    0xc0, 0x1e, 0x00, 0x00, 0x00, 0x01, 0xc0, 0x1c, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x3c, 0x00, 0x00,
    0x00, 0x00, 0xe0, 0x38, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x38, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x78,
    0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00,
    0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00,
    0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0,
    0x00, 0x00, 0x00, 0x00, 0x1f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00};

const unsigned char backBitmap [] PROGMEM = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xc0, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x1f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 
    0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc0, 
    0x07, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x01, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x1f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x07, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xe0, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xc0, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void drawHomePage();              // page 0
void drawSettingsPage();          // page 1
void drawUnitsPage();             // page 2
void drawAlertPage();             // page 3
void drawCalibrationPage();       // page 4
void drawWifiPage();              // page 5
void drawTimedCountPage();        // page 6
void drawTimedCountRunningPage(int duration, int size); // page 7 
void drawDeviceModePage();        // page 8

void drawFrame();
void drawBackButton();
void drawCancelButton();
void drawCloseButton();
void drawBlankDialogueBox();

long EEPROMReadlong(long address);
void EEPROMWritelong(int address, long value); // logging functions
void createJsonFile();
void clearLogs();

void setup()
{
  Serial.begin(38400);
  ts.begin();
  ts.setRotation(2);

  tft.begin();
  tft.setRotation(2);
  tft.fillScreen(ILI9341_BLACK);

  pinMode(D0, OUTPUT); // buzzer switch
  pinMode(D3, OUTPUT); // LED
  digitalWrite(D3, LOW);
  digitalWrite(D0, LOW);

  EEPROM.begin(4096);   // initialize emulated EEPROM sector with 4 kb

  doseUnits = EEPROM.read(saveUnits);
  alarmThreshold = EEPROM.read(saveAlertThreshold);
  conversionFactor = EEPROM.read(saveCalibration);
  deviceMode = EEPROM.read(saveDeviceMode);
  isLogging = EEPROM.read(saveLoggingMode);
  addr = EEPROMReadlong(96);

  SSIDLength = EEPROM.read(saveSSIDLen);
  passwordLength = EEPROM.read(savePWLen);
  channelIDLength = EEPROM.read(saveIDLen);
  writeAPILength = EEPROM.read(saveAPILen);

  for (int i = 10; i < 10 + SSIDLength; i++)
  {
    ssid[i - 10] = EEPROM.read(i);
  }
  Serial.println(ssid);

  for (int j = 30; j < 30 + passwordLength; j++)
  {
    password[j - 30] = EEPROM.read(j);
  }
  Serial.println(password);

  for (int k = 50; k < 50 + channelIDLength; k++)
  {
    channelID[k - 50] = EEPROM.read(k);
  }
  Serial.println(channelID);

  for (int l = 70; l < 70 + writeAPILength; l++)
  {
    channelAPIkey[l - 70] = EEPROM.read(l);
  }
  Serial.println(channelAPIkey);

  attachInterrupt(interruptPin, isr, FALLING);

  drawHomePage();

  if (!deviceMode)
  {
    WiFi.mode( WIFI_OFF );                // turn off wifi
    WiFi.forceSleepBegin();
    delay(1);
  }
  else
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    drawBlankDialogueBox();
    tft.setTextSize(1);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ILI9341_WHITE);
    
    tft.setCursor(38, 140);
    tft.println("Connecting to WiFi..");

    while ((WiFi.status() != WL_CONNECTED) && (attempts < 300))
    {
      delay(100);
      attempts ++;
    }
    if (attempts >= 300)
    {
      deviceMode = 0; 
      tft.setCursor(45, 200);
      tft.println("Failed to connect.");
      delay(1000);
    }
    else
    {
      tft.setCursor(68, 200);
      tft.println("Connected!");
      delay(1000);
    }
    drawHomePage();
  }
}

void loop()
{
  if (page == 0) // homepage
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= 1000)
    {
      previousMillis = currentMillis;

      batteryUpdateCounter ++;     

      if (batteryUpdateCounter == 30){         // update battery level every 30 seconds. Prevents random fluctations of battery level.

        batteryInput = analogRead(A0);
        batteryInput = constrain(batteryInput, 590, 800);
        batteryPercent = map(batteryInput, 590, 800, 0, 100);
        batteryMapped = map(batteryPercent, 100, 0, 212, 233);

        tft.fillRect(212, 6, 22, 10, ILI9341_BLACK);
        if (batteryPercent < 10)
        {
          tft.fillRect(batteryMapped, 6, (234 - batteryMapped), 10, ILI9341_RED);
        }
        else
        {
          tft.fillRect(batteryMapped, 6, (234 - batteryMapped), 10, ILI9341_GREEN); // draws battery icon
        }
        
        batteryUpdateCounter = 0;
        Serial.println(batteryInput);
        Serial.println(batteryPercent);
      }

      count[i] = currentCount;
      i++;
      fastCount[j] = currentCount; // keep concurrent arrays of counts. Use only one depending on user choice
      j++;
      slowCount[k] = currentCount;
      k++;

      if (i == 61)
      {
        i = 0;
      }

      if (j == 6)
      {
        j = 0;
      }

      if (k == 181)
      {
        k = 0;
      }

      if (integrationMode == 2)
      {
        averageCount = (currentCount - slowCount[k]) / 3;
      }

      if (integrationMode == 1)
      {
        averageCount = (currentCount - fastCount[j]) * 12;
      }

      else if (integrationMode == 0)
      {
        averageCount = currentCount - count[i]; // count[i] stores the value from 60 seconds ago
      }

      averageCount = ((averageCount) / (1 - 0.00000333 * float(averageCount))); // accounts for dead time of the geiger tube. relevant at high count rates

      if (doseUnits == 0)
      {
        doseRate = averageCount / float(conversionFactor);
        totalDose = cumulativeCount / (60 * float(conversionFactor));
        
      }
      else if (doseUnits == 1)
      {
        doseRate = averageCount / float(conversionFactor * 10.0);
        totalDose = cumulativeCount / (60 * float(conversionFactor * 10.0)); // 1 mRem == 10 uSv
        
      }

      if (averageCount < conversionFactor/2) // 0.5 uSv/hr
        doseLevel = 0; // determines alert level displayed on homescreen
      else if (averageCount < alarmThreshold * conversionFactor)
        doseLevel = 1;
      else
        doseLevel = 2;

      if (doseRate < 10.0)
      {
        dtostrf(doseRate, 4, 2, dose); // display two digits after the decimal point if value is less than 10
      }
      else if ((doseRate >= 10) && (doseRate < 100))
      {
        dtostrf(doseRate, 4, 1, dose); // display one digit after decimal point when dose is greater than 10
      }
      else if ((doseRate >= 100))
      {
        dtostrf(doseRate, 4, 0, dose); // whole numbers only when dose is higher than 100
      }
      else {
        dtostrf(doseRate, 4, 0, dose);  // covers the rare edge case where the dose rate is sometimes errorenously calculated to be negative
      }
      
      tft.setFont();
      tft.setCursor(44, 52);
      tft.setTextSize(5);
      tft.setTextColor(ILI9341_WHITE, DOSEBACKGROUND);
      tft.println(dose); // display effective dose rate
      tft.setTextSize(1);

      tft.setFont();
      tft.setCursor(73, 122);
      tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
      tft.setTextSize(3);
      tft.println(averageCount); // Display CPM

      if (averageCount < 10)
      {
        tft.fillRect(90, 120, 144, 25, ILI9341_BLACK); // erase numbers that may have been left from previous high readings
      }
      else if (averageCount < 100)
      {
        tft.fillRect(107, 120, 127, 25, ILI9341_BLACK);
      }
      else if (averageCount < 1000)
      {
        tft.fillRect(124, 120, 110, 25, ILI9341_BLACK);
      }
      else if (averageCount < 10000)
      {
        tft.fillRect(141, 120, 93, 25, ILI9341_BLACK);
      }
      else if (averageCount < 100000)
      {
        tft.fillRect(160, 120, 74, 25, ILI9341_BLACK);
      }
      tft.setCursor(80, 192);
      tft.setTextSize(2);
      tft.setTextColor(ILI9341_WHITE, 0x630C);
      tft.println(cumulativeCount); // display total counts since reset

      tft.setCursor(80, 222);
      tft.println(totalDose); // display cumulative dose

      if (doseLevel != previousDoseLevel) // only update alert level if it changed. This prevents flicker
      {
        if (doseLevel == 0)
        {
          tft.drawRect(0, 0, tft.width(), tft.height(), ILI9341_WHITE);
          tft.fillRoundRect(3, 94, 234, 21, 3, 0x2DC6);
          tft.setCursor(15, 104);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(1);
          tft.println("NORMAL BACKGROUND");

          previousDoseLevel = doseLevel;
        }
        else if (doseLevel == 1)
        {
          tft.drawRect(0, 0, tft.width(), tft.height(), ILI9341_WHITE);
          tft.fillRoundRect(3, 94, 234, 21, 3, 0xCE40);
          tft.setCursor(29, 104);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(1);
          tft.println("ELEVATED ACTIVITY");

          previousDoseLevel = doseLevel;
        }
        else if (doseLevel == 2)
        {
          tft.drawRect(0, 0, tft.width(), tft.height(), ILI9341_RED);
          tft.fillRoundRect(3, 94, 234, 21, 3, 0xB8A2);
          tft.setCursor(17, 104);
          tft.setFont(&FreeSans9pt7b);
          tft.setTextColor(ILI9341_WHITE);
          tft.setTextSize(1);
          tft.println("HIGH RADIATION LEVEL");

          previousDoseLevel = doseLevel;
        }
      }
      Serial.println(currentCount);
    } 
    // end of millis()-controlled block that runs once every second. The rest of the code on page 0 runs every loop
    if (currentCount > previousCount)
    {
      if (ledSwitch)
        digitalWrite(D3, HIGH); // trigger buzzer and led if they are activated
      if (buzzerSwitch)
        digitalWrite(D0, HIGH);
      previousCount = currentCount;
      previousMicros = micros();
    }
    currentMicros = micros();
    if (currentMicros - previousMicros >= 200)
    {
      digitalWrite(D3, LOW);
      digitalWrite(D0, LOW);
      previousMicros = currentMicros;
    }

    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched) // A way of "debouncing" the touchscreen. Prevents multiple inputs from single touch
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0); // get touch point and map to screen pixels
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 162 && x < 238) && (y > 259 && y < 318))
      {
        integrationMode ++;
        if (integrationMode == 3)
        {
          integrationMode = 0;
        }
        currentCount = 0;
        previousCount = 0;
        for (int a = 0; a < 61; a++) // reset counts when integretation speed is changed
        {
          count[a] = 0;
        }
        for (int b = 0; b < 6; b++)
        {
          fastCount[b] = 0;
        }
        for (int c = 0; c < 181; c++)
        {
          slowCount[c] = 0;
        }
        if (integrationMode == 0) // change button based on touch and previous state
        {
          tft.fillRoundRect(162, 259, 74, 57, 3, 0x2A86);
          tft.setFont(&FreeSans12pt7b);
          tft.setTextSize(1);
          tft.setCursor(180, 283);
          tft.println("INT");
          tft.setCursor(177, 309);
          tft.println("60 s");
        }
        else if (integrationMode == 1)
        {
          tft.fillRoundRect(162, 259, 74, 57, 3, 0x2A86);
          tft.setFont(&FreeSans12pt7b);
          tft.setTextSize(1);
          tft.setCursor(180, 283);
          tft.println("INT");
          tft.setCursor(184, 309);
          tft.println("5 s");
        }
        else if (integrationMode == 2)
        {
          tft.fillRoundRect(162, 259, 74, 57, 3, 0x2A86);
          tft.setFont(&FreeSans12pt7b);
          tft.setTextSize(1);
          tft.setCursor(180, 283);
          tft.println("INT");
          tft.setCursor(169, 309);
          tft.println("180 s");
        }
      }
      else if ((x > 64 && x < 159) && (y > 259 && y < 318)) // timed count 
      {
        page = 6;
        drawTimedCountPage();
      }
      else if ((x > 190 && x < 238) && (y > 151 && y < 202)) // toggle LED
      {
        ledSwitch = !ledSwitch;
        if (ledSwitch)
        {
          tft.fillRoundRect(190, 151, 46, 51, 3, 0x6269);
          tft.drawBitmap(190, 153, ledOnBitmap, 45, 45, ILI9341_WHITE);
        }
        else
        {
          tft.fillRoundRect(190, 151, 46, 51, 3, 0x6269);
          tft.drawBitmap(190, 153, ledOffBitmap, 45, 45, ILI9341_WHITE);
        }
      }
      else if ((x > 190 && x < 238) && (y > 205 && y < 256)) // toggle buzzer
      {
        buzzerSwitch = !buzzerSwitch;
        if (buzzerSwitch)
        {
          tft.fillRoundRect(190, 205, 46, 51, 3, 0x6269);
          tft.drawBitmap(190, 208, buzzerOnBitmap, 45, 45, ILI9341_WHITE);
        }
        else
        {
          tft.fillRoundRect(190, 205, 46, 51, 3, 0x6269);
          tft.drawBitmap(190, 208, buzzerOffBitmap, 45, 45, ILI9341_WHITE);
        }
      }
      else if ((x > 3 && x < 61) && (y > 259 && y < 316)) // settings button pressed
      {
        page = 1;
        drawSettingsPage();
      }
    }
    
    if (isLogging)
    {
      if(addr < 2100)
      {
        currentLogTime = millis();
        if ((currentLogTime - previousLogTime) >= 600000)   // log every 10 minutes
        {
          EEPROMWritelong(addr, averageCount);
          addr += 4;
          EEPROMWritelong(96, addr); // write current address number to an adress just before the logged data
          previousLogTime = currentLogTime;
          EEPROM.commit();
        }
      }
    }
    if (deviceMode)    // deviceMode is 1 when in monitoring station mode. Uploads CPM to thingspeak every 5 minutes
    {
      currentUploadTime = millis();
      if ((currentUploadTime - previousUploadTime) > 300000)
      {
        previousUploadTime = currentUploadTime;
        if (client.connect(server, 80))
        {
          String postStr = channelAPIkey;
          postStr += "&field2=";
          postStr += String(averageCount);
          postStr += "\r\n\r\n";
          char temp[50] = "X-THINGSPEAKAPIKEY:";
          strcat(temp, channelAPIkey);
          strcat(temp, "\n");
          client.print("POST /update HTTP/1.1\n");
          client.print("Host: api.thingspeak.com\n");
          client.print("Connection: close\n");
          client.print(temp);
          client.print("Content-Type: application/x-www-form-urlencoded\n");
          client.print("Content-Length: ");
          client.print(postStr.length());
          client.print("\n\n");
          client.print(postStr);
          Serial.println(postStr);
        }
        client.stop();
      }
    }
  }
  else if (page == 1) // settings page. all display elements are drawn when drawSettingsPage() is called
  {
    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315)) // back button. draw homepage, reset counts and go back
      {
        currentCount = 0;
        previousCount = 0;
        for (int a = 0; a < 61; a++)
        {
          count[a] = 0; // counts need to be reset to prevent errorenous readings
        }
        for (int b = 0; b < 6; b++)
        {
          fastCount[b] = 0;
        }
        for (int c = 0; c < 181; c++)
        {
          slowCount[c] = 0;
        }
        page = 0;
        drawHomePage();
      }
      else if ((x > 3 && x < 234) && (y > 64 && y < 108))
      {
        page = 2;
        drawUnitsPage();
      }
      else if ((x > 3 && x < 234) && (y > 114 && y < 158))
      {
        page = 3;
        drawAlertPage();
      }
      else if ((x > 3 && x < 234) && (y > 164 && y < 208))
      {
        page = 4;
        drawCalibrationPage();
      }
      else if ((x > 3 && x < 234) && (y > 214 && y < 268))
      {
        page = 5;
        drawWifiPage();
      }
    }
  }
  else if (page == 2) // units page
  {
    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315)) // back button
      {
        page = 1;
        if (EEPROM.read(saveUnits) != doseUnits) // check current EEPROM value and only write if new value is different
        {
          EEPROM.write(saveUnits, doseUnits); // save current units to EEPROM during exit. This will be retrieved at startup
          EEPROM.commit();
        }
        drawSettingsPage();
      }
      else if ((x > 4 && x < 234) && (y > 70 && y < 120))
      {
        doseUnits = 0;
        tft.fillRoundRect(4, 71, 232, 48, 4, 0x2A86);
        tft.setCursor(30, 103);
        tft.println("Sieverts (uSv/hr)");

        tft.fillRoundRect(4, 128, 232, 48, 4, ILI9341_BLACK);
        tft.setCursor(47, 160);
        tft.println("Rems (mR/hr)");
      }
      else if ((x > 4 && x < 234) && (y > 127 && y < 177))
      {
        doseUnits = 1;
        tft.fillRoundRect(4, 71, 232, 48, 4, ILI9341_BLACK);
        tft.setCursor(30, 103);
        tft.println("Sieverts (uSv/hr)");

        tft.fillRoundRect(4, 128, 232, 48, 4, 0x2A86);
        tft.setCursor(47, 160);
        tft.println("Rems (mR/hr)");
      }
    }
  }
  else if (page == 3)        // alert thresold page
  {
    tft.setFont();
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor(151, 146);
    tft.println(alarmThreshold);
    if (alarmThreshold < 10)
      tft.fillRect(169, 146, 22, 22, ILI9341_BLACK);

    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315))
      {
        page = 1;
        if (EEPROM.read(saveAlertThreshold) != alarmThreshold)
        {
          EEPROM.write(saveAlertThreshold, alarmThreshold);
          EEPROM.commit(); // save to EEPROM to be retrieved at startup
        }
        drawSettingsPage();
      }
      else if ((x > 130 && x < 190) && (y > 70 && y < 120))
      {
        alarmThreshold++;
        if (alarmThreshold > 100)
          alarmThreshold = 100;
      }
      else if ((x > 130 && x < 190) && (y > 185 && y < 245))
      {
        alarmThreshold--;
        if (alarmThreshold <= 2)
          alarmThreshold = 2;
      }
    }
  }
  else if (page == 4)     // calibration page
  {
    tft.setFont();
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor(161, 146);
    tft.println(conversionFactor);
    if (conversionFactor < 100)
      tft.fillRect(197, 146, 22, 22, ILI9341_BLACK);

    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315))
      {
        page = 1;
        if (EEPROM.read(saveCalibration) != conversionFactor)
        {
          EEPROM.write(saveCalibration, conversionFactor);
          EEPROM.commit();
        }
        drawSettingsPage();
      }
      else if ((x > 160 && x < 220) && (y > 70 && y < 120))
      {
        conversionFactor++;
      }
      else if ((x > 160 && x < 220) && (y > 185 && y < 245))
      {
        conversionFactor--;
        if (conversionFactor <= 1)
          conversionFactor = 1;
      }
    }
  }
  else if (page == 5)  // Wifi page
  {
    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315))
      {
        page = 1;
        if (EEPROM.read(saveLoggingMode) != isLogging) // check current EEPROM value and only write if new value is different
        {
          EEPROM.write(saveLoggingMode, isLogging); 
          EEPROM.commit();
        }
        drawSettingsPage();
      }
      else if ((x > 3 && x < 237) && (y > 64 && y < 108))  // wifi setup button
      {
        tft.setFont(&FreeSans9pt7b);
        tft.setTextSize(1);

        tft.fillRoundRect(10, 30, 220, 260, 6, ILI9341_BLACK);
        tft.drawRoundRect(10, 30, 220, 260, 6, ILI9341_WHITE);

        tft.setCursor(50, 50);
        tft.println("AP SETUP MODE");
        tft.drawFastHLine(50, 53, 145, ILI9341_WHITE);
        tft.setCursor(20, 80);
        tft.println("With any WiFi capable");
        tft.setCursor(20, 100);
        tft.println("device, connect to");
        tft.setCursor(20, 120);
        tft.println("network \"GC20\" and ");
        tft.setCursor(20, 140);
        tft.println("browse to 192.168.4.1");
        tft.setCursor(20, 160);
        tft.println("Enter credentials");
        tft.setCursor(20, 180);
        tft.println("of your WiFi network");
        tft.setCursor(20, 200);
        tft.println("and the Channel ID and");
        tft.setCursor(20, 220);
        tft.println("write API key of your");
        tft.setCursor(20, 240);
        tft.println("ThingSpeak channel");

        delay(100);
        WiFiManager wifiManager;

        char channelIDSt[20];
        char writeAPISt[20];

        WiFiManagerParameter channel_id("0", "Channel ID", channelIDSt, 20); // create custom parameters for setup
        
        WiFiManagerParameter write_api("1", "Write API", writeAPISt, 20);
        wifiManager.addParameter(&channel_id);
        wifiManager.addParameter(&write_api);

        wifiManager.startConfigPortal("GC20");            // put the esp in AP mode for wifi setup, create a network with name "GC20"

        strcpy(channelIDSt, channel_id.getValue());
        strcpy(writeAPISt, write_api.getValue());

        size_t idLen = String(channelIDSt).length();

        size_t apiLen = String(writeAPISt).length();

        char channelInit = EEPROM.read(4001);  // first character of channelID is stored in EEPROM address 4001
        char apiKeyInit = EEPROM.read(4002);   // Only overwrite channelIDSt and writeAPISt if new value of the first character is different from what was saved.

        if (channelInit != channelIDSt[0])   
        {
          for (unsigned int a = 50; a < 50 + idLen; a++)
          {
            EEPROM.write((a), channelIDSt[a - 50]);
          }
          EEPROM.write(saveIDLen, idLen);
        }

        if(apiKeyInit != writeAPISt[0])
        {
          for (unsigned int b = 70; b < 70 + apiLen; b++)
          {
            EEPROM.write((b), writeAPISt[b - 70]);
          }
          EEPROM.write(saveAPILen, apiLen);
        }

        String ssidString = WiFi.SSID();      // retrieve ssid and password form the WifiManager library
        String passwordString = WiFi.psk();

        size_t ssidLen = ssidString.length();
        size_t passLen = passwordString.length();

        Serial.println(ssidLen);
        Serial.println(passLen);

        char ssidChar[20];
        char passwordChar[20];

        ssidString.toCharArray(ssidChar, ssidLen + 1); 
        passwordString.toCharArray(passwordChar, passLen + 1);

        for (unsigned int a = 10; a < 10 + ssidLen; a++)
        {
          EEPROM.write((a), ssidChar[a - 10]);             // save ssid and ssid length to EEPROM
        }
        EEPROM.write(saveSSIDLen, ssidLen);
        
        for (unsigned int b = 30; b < 30 + passLen; b++)
        {    
          EEPROM.write((b), passwordChar[b - 30]);          // save password and password length to EEPROM
        }
        EEPROM.write(savePWLen, passLen);

        EEPROM.write(4001, channelIDSt[0]);                 // save first characters of channel ID and api key to EEPROM
        EEPROM.write(4002, writeAPISt[0]);

        EEPROM.commit();

        tft.setCursor(16, 265);
        tft.println("Settings saved. Restarting");

        delay(1000);
        
        ESP.reset();
      }
      else if ((x > 3 && x < 237) && (y > 162 && y < 206)) // upload data
      {
        
        drawBlankDialogueBox();
        tft.setCursor(38, 100);
        tft.println("Connecting to Wifi..");
        delay(100);
        Serial.println(ssid);
        Serial.println(password);

        WiFi.begin(ssid, password);

        while (WiFi.status() != WL_CONNECTED) {    // Wait for the Wi-Fi to connect
          delay(100);
        }

        tft.setCursor(36, 160);
        tft.println("Creating JSON file..");
        createJsonFile();                         // reads logged data from EEPROM and creates a json file
        Serial.println(jsonBuffer);
        delay(1000);
        tft.setCursor(70, 220);
        tft.println("Uploading..");
        delay(1000);

        char secondHalf[50] = "\",\"updates\":";      
        strcat(data, channelAPIkey);
        strcat(data, secondHalf);               

        strcat(data,jsonBuffer);                // concatenate strings together and store in array named data
        strcat(data,"}");

        Serial.println(data);

        client.stop();
        String data_length = String(strlen(data)+1);   
        
        if (client.connect(server, 80)) {          // post data to thingspeak
          char temp1[100] = "POST /channels/";
          char temp2[30] = "/bulk_update.json HTTP/1.1";
          
          strcat(temp1, channelID);
          strcat(temp1, temp2);

          client.println(temp1); 
          client.println("Host: api.thingspeak.com");
          client.println("User-Agent: mw.doc.bulk-update (Arduino ESP8266)");
          client.println("Connection: close");
          client.println("Content-Type: application/json");
          client.println("Content-Length: "+data_length);
          client.println();
          client.println(data);
          client.stop();
          
          WiFi.disconnect();
          WiFi.mode( WIFI_OFF );                // turn off wifi
          WiFi.forceSleepBegin();
          delay(1);

          clearLogs();                 // erase logs and re-initialize the json buffer
          tft.setCursor(43, 260);
          tft.println("Resetting Device..");
          delay(1000);
          ESP.reset();                 
        }
        else 
        {
          tft.setCursor(50, 260);
          tft.println("Failed to upload");
          delay(1000);
          ESP.reset();
        }
        
      }
      else if ((x > 3 && x < 237) && (y > 114 && y < 158)) // logging 
      {
        isLogging = !isLogging;
        if (isLogging)
        {
          tft.fillRoundRect(3, 114, 234, 44, 4, 0x3B8F);
          tft.drawRoundRect(3, 114, 234, 44, 4, WHITE);
          tft.setCursor(38, 145);
          tft.println("LOGGING ON");
        }
        else
        {
          tft.fillRoundRect(3, 114, 234, 44, 4, 0xB9C7);
          tft.drawRoundRect(3, 114, 234, 44, 4, WHITE);
          tft.setCursor(33, 145);
          tft.println("LOGGING OFF");
        }
      }
      else if ((x > 3 && x < 237) && (y > 214 && y < 258))  // device mode
      {
        page = 8;
        drawDeviceModePage();
      }
    }
  }
  else if (page == 6) // timed count setup page
  {
    if (interval < 10)
    {
      intervalSize = 1;
    }
    else if (interval < 100)
    {
      intervalSize = 2;
    }
    else 
    {
      intervalSize = 3;
    }
    
    tft.setFont();
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setCursor((185 - (intervalSize - 1) * 11), 146);
    tft.println(interval);

    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315))
      {
        page = 0;
        drawHomePage();
        currentCount = 0;
        previousCount = 0;
        for (int a = 0; a < 60; a++)
        {
          count[a] = 0; // counts need to be reset to prevent errorenous readings
        }
        for (int b = 0; b < 5; b++)
        {
          fastCount[b] = 0;
        }
        for (int c = 0; c < 180; c++)
        {
          slowCount[c] = 0;
        }
      }
      else if ((x > 145 && x < 235) && (y > 271 && y < 315))
      {
        page = 7;
        drawTimedCountRunningPage(interval, intervalSize);
      }
      else if ((x > 160 && x < 220) && (y > 70 && y < 120))
      {
        interval += 5;
        if (interval >= 995)
        {
          interval = 995;
        }
        tft.fillRect(160, 130, 70, 40, ILI9341_BLACK);
      }
      else if ((x > 160 && x < 220) && (y > 185 && y < 245))
      {
        interval -= 5;
        if (interval <= 5)
        {
          interval = 5;
        }
        tft.fillRect(160, 130, 70, 40, ILI9341_BLACK);
      }
    }
  }
  else if (page == 7) // timed count running page
  {
    elapsedTime = millis() - startMillis;
    if(elapsedTime < intervalMillis)
    {
      if((millis() - previousMillis) >= 1000)
      {
        previousMillis = millis();

        tft.setFont();
        tft.setTextSize(3);
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
        tft.setCursor(101, 181);
        tft.println(currentCount);

        cpm = float(currentCount) / float((1 + elapsedTime) / 60000.0);
        
        tft.setCursor(101, 226);
        tft.println(cpm);

        if(cpm < 10)
        {
          tft.fillRect(170, 225, 50, 40, ILI9341_BLACK);
        }
        else if(cpm < 100)
        {
          tft.fillRect(190, 225, 35, 40, ILI9341_BLACK);
        }
        else if(cpm < 1000)
        {
          tft.fillRect(209, 225, 18, 40, ILI9341_BLACK);
        }
      }
      progress = map(elapsedTime, 0, intervalMillis, 0, 217);
      tft.fillRect(12, 105, progress, 16, 0x25A6);
    }
    else 
    {
      if (completed == 0)
      {
        drawCloseButton();
        completed = 1;
      }
    }
    
    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 70 && x < 170) && (y > 271 && y < 315))
      {
        page = 0;
        drawHomePage();
        currentCount = 0;
        previousCount = 0;
        for (int a = 0; a < 60; a++)
        {
          count[a] = 0; // counts need to be reset to prevent errorenous readings
        }
        for (int b = 0; b < 5; b++)
        {
          fastCount[b] = 0;
        }
        for (int c = 0; c < 180; c++)
        {
          slowCount[c] = 0;
        }
      }
    }
  }
  else if (page == 8)          // device mode selection page
  {
    if (!ts.touched())
      wasTouched = 0;
    if (ts.touched() && !wasTouched)
    {
      wasTouched = 1;
      TS_Point p = ts.getPoint();
      x = map(p.x, TS_MINX, TS_MAXX, 240, 0);
      y = map(p.y, TS_MINY, TS_MAXY, 320, 0);

      if ((x > 4 && x < 62) && (y > 271 && y < 315)) // back button
      {
        page = 5;
        if (EEPROM.read(saveDeviceMode) != deviceMode) // check current EEPROM value and only write if new value is different
        {
          EEPROM.write(saveDeviceMode, deviceMode); 
          EEPROM.commit();
        }
        drawWifiPage();
      }
      else if ((x > 4 && x < 234) && (y > 70 && y < 120))
      {
        deviceMode = 0;
        tft.setFont(&FreeSans12pt7b);
        tft.fillRoundRect(4, 71, 232, 48, 4, 0x2A86);
        tft.setCursor(13, 103);
        tft.println("GEIGER COUNTER");

        tft.fillRoundRect(4, 128, 232, 48, 4, ILI9341_BLACK);
        tft.setCursor(30, 160);
        tft.println("MON. STATION");

      }
      else if ((x > 4 && x < 234) && (y > 127 && y < 177))
      {
        deviceMode = 1;
        tft.setFont(&FreeSans12pt7b);
        tft.fillRoundRect(4, 71, 232, 48, 4, ILI9341_BLACK);
        tft.setCursor(13, 103);
        tft.println("GEIGER COUNTER");

        tft.fillRoundRect(4, 128, 232, 48, 4, 0x2A86);
        tft.setCursor(30, 160);
        tft.println("MON. STATION");

      }
    }
  }
}

void drawHomePage()
{

  tft.fillRect(1, 21, 237, 298, ILI9341_BLACK);
  tft.drawRect(0, 0, tft.width(), tft.height(), ILI9341_WHITE);

  tft.drawRoundRect(210, 4, 26, 14, 3, ILI9341_WHITE);
  tft.drawLine(209, 8, 209, 13, ILI9341_WHITE); // Battery symbol
  tft.drawLine(208, 8, 208, 13, ILI9341_WHITE);
  tft.fillRect(212, 6, 22, 10, ILI9341_BLACK);

  tft.fillRect(batteryMapped, 6, (234 - batteryMapped), 10, ILI9341_GREEN);
  
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_CYAN);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, 16);
  tft.println("GC-20");
  tft.setTextColor(ILI9341_WHITE);
  
  tft.setFont();
  tft.setTextSize(2);
  tft.setCursor(118, 4);
  tft.println("+");
  tft.setTextSize(1);
  tft.setFont(&FreeSans9pt7b);

  tft.drawBitmap(103, 2, betaBitmap, 18, 18, ILI9341_WHITE);
  tft.drawBitmap(128, 2, gammaBitmap, 12, 18, ILI9341_WHITE);

  tft.drawLine(1, 20, 238, 20, ILI9341_WHITE);
  tft.fillRoundRect(3, 23, 234, 69, 3, DOSEBACKGROUND);
  tft.setCursor(16, 40);
  tft.println("EFFECTIVE DOSE RATE:");
  tft.setCursor(165, 85);
  tft.setFont(&FreeSans12pt7b);
  if (doseUnits == 0)
  {
    tft.println("uSv/hr");
  }
  else if (doseUnits == 1)
  {
    tft.println("mR/hr");
  }

  tft.fillRoundRect(3, 94, 234, 21, 3, 0x2DC6);
  tft.setCursor(15, 110);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.println("NORMAL BACKGROUND");

  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(7, 141);
  tft.println("CPM:");
  tft.drawRoundRect(3, 117, 234, 32, 3, DOSEBACKGROUND);

  tft.fillRoundRect(3, 151, 185, 105, 4, 0x630C);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(9, 171);
  tft.println("CUMULATIVE DOSE");
  tft.setCursor(7, 205);
  tft.println("Counts:");
  if (doseUnits == 0)
  {
    tft.setCursor(34, 235);
    tft.println("uSv:");
  }
  else if (doseUnits == 1)
  {
    tft.setCursor(37, 235);
    tft.println("mR:");
  }

  tft.fillRoundRect(3, 259, 58, 57, 3, 0x3B8F);
  tft.drawBitmap(1, 257, settingsBitmap, 60, 60, ILI9341_WHITE);

  tft.fillRoundRect(64, 259, 95, 57, 3, 0x6269);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.setCursor(74, 284);
  tft.println("TIMED");
  tft.setCursor(70, 309);
  tft.println("COUNT");

  if (integrationMode == 0)
  {
    tft.fillRoundRect(162, 259, 74, 57, 3, 0x2A86);
    tft.setCursor(180, 283);
    tft.println("INT");
    tft.setCursor(177, 309);
    tft.println("60 s");
  }
  else if (integrationMode == 1)
  {
    tft.fillRoundRect(162, 259, 74, 57, 3, 0x2A86);
    tft.setCursor(180, 283);
    tft.println("INT");
    tft.setCursor(184, 309);
    tft.println("5 s");
  }
  else if (integrationMode == 2)
  {
    tft.fillRoundRect(162, 259, 74, 57, 3, 0x2A86);
    tft.setCursor(180, 283);
    tft.println("INT");
    tft.setCursor(169, 309);
    tft.println("180 s");
  }

  if (ledSwitch)
  {
    tft.fillRoundRect(190, 151, 46, 51, 3, 0x6269);
    tft.drawBitmap(190, 153, ledOnBitmap, 45, 45, ILI9341_WHITE);
  }
  else if (!ledSwitch)
  {
    tft.fillRoundRect(190, 151, 46, 51, 3, 0x6269);
    tft.drawBitmap(190, 153, ledOffBitmap, 45, 45, ILI9341_WHITE);
  }
  if (buzzerSwitch)
  {
    tft.fillRoundRect(190, 205, 46, 51, 3, 0x6269);
    tft.drawBitmap(190, 208, buzzerOnBitmap, 45, 45, ILI9341_WHITE);
  }
  else if (!buzzerSwitch)
  {
    tft.fillRoundRect(190, 205, 46, 51, 3, 0x6269);
    tft.drawBitmap(190, 208, buzzerOffBitmap, 45, 45, ILI9341_WHITE);
  }
  tft.setFont(&FreeSans9pt7b);
  if (isLogging)
  {
    tft.setCursor(175, 16);
    tft.println("L");
  }
  else
  {
    tft.fillRect(175, 2, 18, 18, ILI9341_BLACK);
  }
  
  if (deviceMode)
  {
    tft.drawBitmap(188, 1, wifiBitmap, 19, 19, ILI9341_WHITE);
  }
  else
  {
    tft.fillRect(188, 1, 19, 19, ILI9341_BLACK);
  }
}

void drawSettingsPage()
{
  digitalWrite(D3, LOW);
  digitalWrite(D0, LOW);

  drawFrame();

  tft.fillRoundRect(3, 23, 234, 35, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(57, 48);
  tft.println("SETTINGS");
  tft.drawFastHLine(59, 51, 117, WHITE);

  tft.fillRoundRect(3, 64, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 64, 234, 44, 4, WHITE);
  tft.setCursor(44, 94);
  tft.println("DOSE UNITS");

  tft.fillRoundRect(3, 114, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 114, 234, 44, 4, WHITE);
  tft.setCursor(5, 145);
  tft.println("ALERT THRESHOLD");

  tft.fillRoundRect(3, 164, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 164, 234, 44, 4, WHITE);
  tft.setCursor(37, 194);
  tft.println("CALIBRATION");

  tft.fillRoundRect(3, 214, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 214, 234, 44, 4, WHITE);
  tft.setCursor(8, 244);
  tft.println("LOGGING AND WIFI");

  drawBackButton();
}

void drawUnitsPage()
{
  drawFrame();

  tft.fillRoundRect(3, 23, 234, 40, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(84, 51);
  tft.println("UNITS");
  tft.drawFastHLine(86, 55, 71, WHITE);

  drawBackButton();

  tft.drawRoundRect(3, 70, 234, 50, 4, WHITE);
  if (doseUnits == 0)
    tft.fillRoundRect(4, 71, 232, 48, 4, 0x2A86);
  tft.setCursor(30, 103);
  tft.println("Sieverts (uSv/hr)");

  tft.drawRoundRect(3, 127, 234, 50, 4, WHITE);
  if (doseUnits == 1)
    tft.fillRoundRect(4, 128, 232, 48, 4, 0x2A86);
  tft.setCursor(47, 160);
  tft.println("Rems (mR/hr)");
}

void drawAlertPage()
{
  drawFrame();

  tft.fillRoundRect(3, 23, 234, 40, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(4, 51);
  tft.println("ALERT THRESHOLD");
  tft.drawFastHLine(5, 55, 229, WHITE);

  drawBackButton();
  
  tft.setCursor(30, 164);
  tft.println("uSv/hr:");

  tft.drawRoundRect(130, 70, 60, 60, 4, ILI9341_WHITE);
  tft.fillRoundRect(131, 71, 58, 58, 4, 0x2A86);
  tft.drawRoundRect(130, 185, 60, 60, 4, ILI9341_WHITE);
  tft.fillRoundRect(131, 186, 58, 58, 4, 0x2A86);

  tft.setCursor(140, 113);
  tft.setTextSize(3);
  tft.println("+");
  tft.setCursor(148, 232);
  tft.println("-");
  tft.setTextSize(1);
}

void drawCalibrationPage()
{
  drawFrame();

  tft.fillRoundRect(3, 23, 234, 40, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(47, 51);
  tft.println("CALIBRATE");
  tft.drawFastHLine(48, 55, 133, WHITE);

  drawBackButton();

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(8, 154);
  tft.println("Conversion Factor");
  tft.setCursor(8, 174);
  tft.println("(CPM per uSv/hr)");

  tft.drawRoundRect(160, 70, 60, 60, 4, ILI9341_WHITE);
  tft.fillRoundRect(161, 71, 58, 58, 4, 0x2A86);
  tft.drawRoundRect(160, 185, 60, 60, 4, ILI9341_WHITE);
  tft.fillRoundRect(161, 186, 58, 58, 4, 0x2A86);

  tft.setCursor(170, 113);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(3);
  tft.println("+");
  tft.setCursor(178, 232);
  tft.println("-");
  tft.setTextSize(1);
}

void drawWifiPage()
{
  drawFrame();

  drawBackButton();

  tft.fillRoundRect(3, 23, 234, 35, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(7, 48);
  tft.println("LOGGING AND WIFI");
  tft.drawFastHLine(8, 51, 222, WHITE);

  tft.fillRoundRect(3, 64, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 64, 234, 44, 4, WHITE);
  tft.setCursor(48, 94);
  tft.println("WIFI SETUP");

  if (isLogging)
  {
    tft.fillRoundRect(3, 114, 234, 44, 4, 0x3B8F);
    tft.drawRoundRect(3, 114, 234, 44, 4, WHITE);
    tft.setCursor(38, 145);
    tft.println("LOGGING ON");
  }
  else
  {
    tft.fillRoundRect(3, 114, 234, 44, 4, 0xB9C7);
    tft.drawRoundRect(3, 114, 234, 44, 4, WHITE);
    tft.setCursor(33, 145);
    tft.println("LOGGING OFF");
  }
  
  tft.fillRoundRect(3, 164, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 164, 234, 44, 4, WHITE);
  tft.setCursor(31, 194);
  tft.println("UPLOAD DATA");

  tft.fillRoundRect(3, 214, 234, 44, 4, 0x2A86);
  tft.drawRoundRect(3, 214, 234, 44, 4, WHITE);
  tft.setCursor(35, 244);
  tft.println("DEVICE MODE");

  if (addr > 2000)
  {
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(80, 297);
    tft.println("Log memory full"); 
  }
}

void drawTimedCountPage()
{
  drawFrame();

  drawBackButton();

  tft.fillRoundRect(145, 271, 92, 45, 3, 0x3B8F);
  tft.drawRoundRect(145, 271, 92, 45, 3, ILI9341_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(149, 302);
  tft.println("BEGIN!");

  tft.fillRoundRect(3, 23, 234, 40, 3, 0x3B8F);
  tft.setCursor(34, 51);
  tft.println("TIMED COUNT");
  tft.drawFastHLine(35, 55, 163, WHITE);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(5, 162);
  tft.println("Duration (minutes):");

  tft.drawRoundRect(160, 70, 60, 60, 4, ILI9341_WHITE);
  tft.fillRoundRect(161, 71, 58, 58, 4, 0x2A86);
  tft.drawRoundRect(160, 185, 60, 60, 4, ILI9341_WHITE);
  tft.fillRoundRect(161, 186, 58, 58, 4, 0x2A86);

  tft.setCursor(170, 113);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(3);
  tft.println("+");
  tft.setCursor(178, 232);
  tft.println("-");
  tft.setTextSize(1);

  cpm = 0;
  progress = 0;

}

void drawTimedCountRunningPage(int duration, int size)
{
  drawFrame();

  drawCancelButton();

  tft.fillRoundRect(3, 23, 234, 40, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setTextSize(1);
  tft.setCursor(34, 51);
  tft.println("TIMED COUNT");
  tft.drawFastHLine(35, 55, 163, WHITE);

  tft.drawRoundRect(3, 66, 234, 95, 4, ILI9341_WHITE);
  tft.drawRect(10, 103, 220, 20, ILI9341_WHITE);
  tft.drawRoundRect(3, 164, 234, 103, 4, ILI9341_WHITE);

  tft.setCursor(58, 90);
  tft.println("Progress:");
  tft.setCursor(13, 150);
  tft.println("Duration:");
  tft.setCursor(115, 150);
  tft.println(duration);
  tft.setCursor((135 + (size - 1)*15), 150);
  tft.println("min");
  tft.setCursor(15, 200);
  tft.println("Counts:");
  tft.setCursor(37, 245);
  tft.println("CPM:");

  currentCount = 0;
  startMillis = millis();
  intervalMillis = duration * 60000;
  completed = 0;
}

void drawDeviceModePage()
{
  drawFrame();

  tft.fillRoundRect(3, 23, 234, 40, 3, 0x3B8F);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(34, 51);
  tft.println("DEVICE MODE");
  tft.drawFastHLine(35, 57, 160, WHITE);

  drawBackButton();

  tft.drawRoundRect(3, 70, 234, 50, 4, WHITE);
  if (deviceMode == 0)
  tft.fillRoundRect(4, 71, 232, 48, 4, 0x2A86);
  tft.setCursor(13, 103);
  tft.println("GEIGER COUNTER");

  tft.drawRoundRect(3, 127, 234, 50, 4, WHITE);
  if (deviceMode == 1)
  tft.fillRoundRect(4, 128, 232, 48, 4, 0x2A86);
  tft.setCursor(30, 160);
  tft.println("MON. STATION");

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(20, 200);
  tft.println("Press Back button and");
  tft.setCursor(20, 220);
  tft.println("reset device for changes");
  tft.setCursor(20, 240);
  tft.println("to take effect");
}

void isr() // interrupt service routine
{
  if ((micros() - 200) > previousIntMicros){
    currentCount++;
    cumulativeCount++;
  }
  previousIntMicros = micros();
}

void drawBackButton(){
  tft.fillRoundRect(4, 271, 62, 45, 3, 0x3B8F);
  tft.drawRoundRect(4, 271, 62, 45, 3, ILI9341_WHITE);
  tft.drawBitmap(4, 271, backBitmap, 62, 45, ILI9341_WHITE);
}

void drawFrame(){
  tft.fillRect(2, 21, 236, 298, ILI9341_BLACK);
  tft.drawRect(0, 0, tft.width(), tft.height(), ILI9341_WHITE);

  tft.drawRoundRect(210, 4, 26, 14, 3, ILI9341_WHITE);
  tft.drawLine(209, 8, 209, 13, ILI9341_WHITE); // Battery symbol
  tft.drawLine(208, 8, 208, 13, ILI9341_WHITE);
  tft.fillRect(212, 6, 22, 10, ILI9341_BLACK);
  tft.fillRect(batteryMapped, 6, (234 - batteryMapped), 10, ILI9341_GREEN);
  
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, 16);
  tft.setTextColor(ILI9341_CYAN);
  
  tft.setTextSize(1);
  tft.println("GC-20");
  tft.setTextColor(ILI9341_WHITE);

  tft.setFont();
  tft.setTextSize(2);
  tft.setCursor(118, 4);
  tft.println("+");
  tft.setTextSize(1);
  tft.setFont(&FreeSans9pt7b);

  tft.drawBitmap(103, 2, betaBitmap, 18, 18, ILI9341_WHITE);
  tft.drawBitmap(128, 2, gammaBitmap, 12, 18, ILI9341_WHITE);

  tft.drawLine(1, 20, 238, 20, ILI9341_WHITE);
  tft.setFont(&FreeSans9pt7b);
  if (isLogging)
  {
    tft.setCursor(175, 16);
    tft.println("L");
  }
  else
  {
    tft.fillRect(175, 2, 18, 18, ILI9341_BLACK);
  }
  
  if (deviceMode)
  {
    tft.drawBitmap(188, 1, wifiBitmap, 18, 18, ILI9341_WHITE);
  }
  else
  {
    tft.fillRect(188, 1, 19, 19, ILI9341_BLACK);
  }
}

void drawCancelButton()
{
  tft.fillRoundRect(70, 271, 100, 45, 3, 0xB9C7);
  tft.drawRoundRect(70, 271, 100, 45, 3, ILI9341_WHITE);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(72, 302);
  tft.println("CANCEL");
}

void drawCloseButton()
{
  tft.fillRoundRect(70, 271, 100, 45, 3, 0x3B8F);
  tft.drawRoundRect(70, 271, 100, 45, 3, ILI9341_WHITE);
  tft.setTextSize(1);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(79, 302);
  tft.println("CLOSE");
}

long EEPROMReadlong(long address) {
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);
 
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void EEPROMWritelong(int address, long value) {
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);
 
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

void createJsonFile()
{
  Serial.println(addr);
  for (int i = 100; i < addr; i += 4)
  {
    int count = EEPROMReadlong(i);

    int deltaT = 600;
    strcat(jsonBuffer,"{\"delta_t\":");
    size_t lengthT = String(deltaT).length();
    char temp[10];
    String(deltaT).toCharArray(temp,lengthT + 1);
    strcat(jsonBuffer,temp);
    strcat(jsonBuffer,",");

    strcat(jsonBuffer, "\"field1\":");
    lengthT = String(count).length();
    String(count).toCharArray(temp, lengthT + 1);

    strcat(jsonBuffer,temp);
    strcat(jsonBuffer,"},");

  }
  size_t len = strlen(jsonBuffer);
  jsonBuffer[len-1] = ']';

}

void drawBlankDialogueBox()
{
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);

  tft.fillRoundRect(20, 50, 200, 220, 6, ILI9341_BLACK);
  tft.drawRoundRect(20, 50, 200, 220, 6, ILI9341_WHITE);

}

void clearLogs()
{
  for (int j = 100; j < 4000; j ++)
  {
    EEPROMWritelong(j, 0);
  }
  for (int k = 1; k < 1000; k++) // keep the first character in jsonBuffer: "["
  {
    jsonBuffer[k] = 0;
  }
  addr = 100;
  EEPROMWritelong(96, addr);
  EEPROM.write(saveLoggingMode, 0);
  EEPROM.commit();
  isLogging = 0;
}