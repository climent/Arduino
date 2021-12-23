
#include <Arduino_JSON.h>
#include <SPI.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C_Hangul.h>

#define countof(a) (sizeof(a) / sizeof(a[0]))

#define BUTTON_PIN 0

#include "config.h"
#ifndef CONFIG_H
char ssid[] = "network";
char pass[] = "password";
String apiKey = "apikey";
#endif

String location = "dublin,IE";
int status = WL_IDLE_STATUS; 
char server[] = "api.openweathermap.org";     

int delay_time =          20; // secs, update NTP time delay
int delay_weather_time = 600; // secs, update openweather time delay

bool first_request = true;

int summertimedelta;

char displaybuffer[4] = {' ', ' ', ' ', ' '};

bool dot_secs = false;

unsigned long now_time =              0;
unsigned long last_ntp_received =     1;
unsigned long epoch =                 0;
unsigned long last_weather_received = 1;
bool initial_request =             true;

// Initializations
WiFiUDP udp;
NTPClient timeClient(udp);

WiFiClient client; 

LiquidCrystal_I2C_Hangul lcd(0x27,16,2); // set the LCD address to 0x27 for a 16 chars and 2 line display

void setup()
{
  Serial.begin(115200);
  Serial.println();
  delay(1000);

  // initialize the lcd
  lcd.init();
  lcd.backlight();

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  lcd.setCursor(0,0);
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print(ssid);

  WiFi.begin(ssid, pass);
  
  unsigned long check_wifi_status = millis();
  bool wifi_connected = false;
  bool blink = false;
  while (!wifi_connected) {
    if (millis() - check_wifi_status > 500) {
      if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
      }
      delay(500);
      check_wifi_status = millis();
      Serial.print(".");
      lcd.setCursor(15,0);
      blink == true ? blink = false : blink = true;
      if (blink) {
        lcd.print(".");
      } else {
        lcd.print("o");
      }
    }
  }
  Serial.println("");
  lcd.clear();

  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // NTP initialization
  Serial.println("Starting NTP client");
  timeClient.begin();
  Serial.println("Initial NTP request...");
  while (! timeClient.forceUpdate()) {
    Serial.print(".");
    delay(1000);
  }
  
  // Initial weather request
  lcd.setCursor(0,0);
  lcd.print("Retrieving");
  lcd.setCursor(0,1);
  lcd.print("temp");
  getWeather();
  lcd.setCursor(0,0);
  lcd.print("                ");
}

void loop()
{
  now_time = millis();
  char d_mon_yr[12];
  char tim_set[6];

  //Request to NIST server should be done every $delay_time interval.
  if (now_time - last_ntp_received > delay_time * 1000 || initial_request == true) {

    if (timeClient.update() || initial_request == true) {
      initial_request = false;
      last_ntp_received = millis();
      epoch = timeClient.getEpochTime();

      time_t t = epoch;
      delay_time = (60 - second(t));

      Serial.println("GMT date/time as received from NTP:");

      // get month, day and year 
      //snprintf_P(d_mon_yr, countof(d_mon_yr), PSTR("%02u %s %04u"), day(t), monthShortStr(month(t)), year(t));
      //Serial.println(d_mon_yr);

      // get time
      // char tim_set[9];
      // snprintf_P(tim_set, countof(tim_set), PSTR("%02u:%02u:%02u"), hour(t), minute(t), second(t));
      //snprintf_P(tim_set, countof(tim_set), PSTR("%02u:%02u"), hour(t), minute(t));
      //Serial.println(tim_set);

      lcdPrintDay(weekday(t));
      lcd.setCursor(5,0);
      lcd.print(d_mon_yr);
      lcd.setCursor(0,1);
      lcd.print(tim_set);
    }
  }

  epoch = timeClient.getEpochTime();
  second(epoch) % 2 == 1 ? dot_secs = true : dot_secs = false;
  if (dot_secs) {
    snprintf_P(tim_set, countof(tim_set), PSTR("%02u %02u"), hour(epoch), minute(epoch));
  } else {
    snprintf_P(tim_set, countof(tim_set), PSTR("%02u:%02u"), hour(epoch), minute(epoch));
  }
  lcd.setCursor(0,1);
  lcd.print(tim_set);


  if (now_time - last_weather_received > delay_weather_time * 1000) {
    getWeather();
    last_weather_received = millis();
  }
}


void getWeather() { 
  Serial.println("\nStarting connection to server..."); 
  // if you get a connection, report back via serial: 
  if (client.connect(server, 80)) { 
    Serial.println("connected to weather server"); 
    // Make a HTTP request: 
    client.print("GET /data/2.5/weather?"); 
    client.print("q="+location); 
    client.print("&appid="+apiKey); 
    // client.print("&cnt=2"); 
    client.println("&units=metric"); 
    client.println("Host: api.openweathermap.org"); 
    client.println("Connection: close"); 
    client.println(); 
  } else { 
    Serial.println("unable to connect"); 
  } 
  Serial.println("request sent");
  String line = ""; 
  while (client.connected()) { 
    line = client.readStringUntil('\n');
  }
  // Serial.println("response:");
  // Serial.println(line);
  // Serial.println("=========");
  JSONVar doc = JSON.parse(line);
  // JSON.typeof(jsonVar) can be used to get the type of the var
  if (JSON.typeof(doc) == "undefined") {
    Serial.println("Parsing input failed!");
    lcd.setCursor(11,1);
    lcd.print("T:XXX");
    return;
  }

  Serial.println(doc["main"]["temp"]);
  lcd.setCursor(10,1);
  lcd.print("T:");
  lcd.print(doc["main"]["temp"]);
} 

void lcdPrintDay(int day) {
    lcd.setCursor(0,0);
    switch (day) {
    case 1:
      lcd.print("Sun,");
      break;
    case 2:
      lcd.print("Mon,");
      break;
    case 3:
      lcd.print("Tue,");
      break;
    case 4:
      lcd.print("Wed,");
      break;
    case 5:
      lcd.print("Thu,");
      break;
    case 6:
      lcd.print("Fri,");
      break;
    case 7:
      lcd.print("Sat,");
      break;
    default:
      lcd.print("N/A,");
      break;
  }
}
