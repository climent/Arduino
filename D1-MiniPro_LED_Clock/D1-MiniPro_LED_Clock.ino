// https://learn.adafruit.com/14-segment-alpha-numeric-led-featherwing/usage
// https://github.com/adafruit/Adafruit_LED_Backpack/blob/master/Adafruit_LEDBackpack.cpp

#include <TimeLib.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#define countof(a) (sizeof(a) / sizeof(a[0]))

#define BUTTON_PIN 0

Adafruit_AlphaNum4 matrix = Adafruit_AlphaNum4();

#include "config.h"
#ifndef CONFIG_H
char ssid[] = "network";
char pass[] = "password";
#endif

int hours =       0;
int mins  =       0;
int secs  =       0;
int old_secs  =   0;
int delay_time = 60; // secs, update NTP time delay

bool print_secs = false;
bool first_request = true;

int brightness[2] = {1, 15};
int brightness_level = 1;

int summertimedelta;

WiFiUDP udp;
NTPClient timeClient(udp);

uint DST_value =  0;
uint TZ_value =   0;

char displaybuffer[4] = {' ', ' ', ' ', ' '};

bool dot_secs = false;

unsigned long now_time = 0;
unsigned long last_ntp_received =  1;
unsigned long epoch = 0;
bool initial_request = true;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  delay(1000);
  
  // Matrix
  matrix.begin(0x70);  // pass in the address
  // matrix.setBrightness(8);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  unsigned long updated_dot = millis();
  unsigned long check_wifi_status = millis();
  bool wifi_connected = false;
  int dot = 0;
  while (!wifi_connected) {
    if (millis() - check_wifi_status > 500) {
      if (WiFi.status() == WL_CONNECTED) {
        wifi_connected = true;
      }
      delay(500);
      check_wifi_status = millis();
      Serial.print(".");
    }
    if (millis() - updated_dot > 500) {
      for (int i = 0; i < 4; i++) {
        matrix.writeDigitRaw(i, 0x0);
      }
      matrix.writeDigitRaw(dot, 0x4000);
      matrix.writeDisplay();
      dot++;
      updated_dot = millis();
      if (dot == 4) dot = 0;
    }
  }
  Serial.println("");
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
}

void loop()
{
  // Blink the dot at the seconds
  now_time = millis();
  secs = now_time / 1000;

  int event	= ButtonReadEvent();
	switch(event) {
		case 1:
			Serial.println("  Brightness button pressed.");
      brightness_level == 0 ? brightness_level = 1 : brightness_level = 0;
      matrix.setBrightness(brightness[brightness_level]);
			break;
		case 2:
			Serial.print("  DST value changed: ");
      DST_value == 1 ? DST_value = 0 : DST_value = 1;
      if (DST_value) { Serial.println("Winter --> Summer"); }
      if (! DST_value) { Serial.println("Summer --> Winter"); }
			break;
    case 3:
      TZ_value++;
      if (TZ_value > 23) { TZ_value = 0; }
      Serial.print("TZ value changed to ");
      Serial.println(TZ_value);
      break;
	}
  DST_value == 1 ?  summertimedelta = 1 : summertimedelta = 0;

  //Request to NIST server should be done every $delay_time interval.
  if (now_time - last_ntp_received > delay_time * 1000 || initial_request == true) {
    if (timeClient.update() || initial_request == true) {
      initial_request = false;
      last_ntp_received = millis();
      epoch = timeClient.getEpochTime();

      time_t t = epoch;
      delay_time = (60 - second(t));

      char tim_set[9];
      // get time
      snprintf_P(tim_set, countof(tim_set), PSTR("%02u:%02u:%02u"), hour(t), minute(t), second(t));
      // Serial.println(tim_set);
      Serial.println("==============================================");
      Serial.println("Setting RTC with GMT time as received from NTP");
      Serial.println(tim_set);
      Serial.println("==============================================");
    
      // Localize time with TZ and DST delta
      hours = ((epoch + ((TZ_value + summertimedelta) * 3600)) % 86400L) / 3600;
      mins = (epoch % 3600) / 60;
      secs = (epoch % 60);

      Serial.print("Local time: ");
      Serial.print(hours); 
      Serial.print(":");
      if (mins < 10) Serial.print("0");
      Serial.print(mins);
      if (print_secs) {
        Serial.print(":");
        if (secs < 10) Serial.print("0");
        Serial.print(secs);
      }
      if (DST_value == 0 || DST_value == 1) {
        if (DST_value == 0) Serial.println(" DST winter");
        if (DST_value == 1) Serial.println(" DST summer");
      } else {
        Serial.print("DST_value: ");
        Serial.print(DST_value);
      }
      Serial.println("");
    }
  }

  old_secs = secs;
  epoch = timeClient.getEpochTime();
  secs = second(epoch);

  int fhours = hours + TZ_value + summertimedelta;
  if (fhours > 23) { fhours = fhours - 24; }

  if (old_secs > 58 && secs == 0) {
    // Fancy transition to the next minute
    DisplayCharacter(' ');
    DisplayCharacter(' ');
    DisplayCharacter(' ');
    DisplayCharacter(' ');
    DisplayCharacter(drawdigit(fhours / 10));
    DisplayCharacter(drawdigit(fhours - ((fhours / 10) * 10)));
    DisplayCharacter(drawdigit(mins / 10));
    DisplayCharacter(drawdigit(mins - ((mins / 10) * 10)));
  }
  secs % 2 == 1 ? dot_secs = true : dot_secs = false;
  //Draw the first digit of the hour
  matrix.writeDigitAscii(0, drawdigit(fhours / 10));
  //Draw the second digit of the hour
  matrix.writeDigitAscii(1, drawdigit(fhours - ((fhours / 10) * 10)), dot_secs);
  //Draw the first digit of the minute
  matrix.writeDigitAscii(2, drawdigit(mins / 10));
  //Draw the second digit of the minute
  matrix.writeDigitAscii(3, drawdigit(mins - ((mins / 10) * 10)));
  matrix.writeDisplay();
}

// HELPER FUNCTIONS ============================================================

// DISPLAY =====================================================================

void DisplayCharacter(char c)
{
  // scroll down display
  displaybuffer[0] = displaybuffer[1];
  displaybuffer[1] = displaybuffer[2];
  displaybuffer[2] = displaybuffer[3];
  displaybuffer[3] = c;
 
  // set every digit to the buffer
  matrix.writeDigitAscii(0, displaybuffer[0]);
  matrix.writeDigitAscii(1, displaybuffer[1]);
  matrix.writeDigitAscii(2, displaybuffer[2]);
  matrix.writeDigitAscii(3, displaybuffer[3]);
 
  // write it out!
  matrix.writeDisplay();
  delay(100);
}

char drawdigit(int n)
{
  switch(n){
    case 1:
    return '1';
    case 2:
    return '2';
    case 3:
    return '3';
    case 4:
    return '4';
    case 5:
    return 'S';
    case 6:
    return '6';
    case 7:
    return '7';
    case 8:
    return '8';
    case 9:
    return '9';
    case 0:
    return '0';
  }
}

// BUTTON ======================================================================

unsigned long millisBtwnPushes = 100;
unsigned long lastPush;

int state;
int lastState = HIGH;

unsigned long lastCycle = millis();
unsigned long eepromMillis;
bool          eepromOutdated;

// Button timing variables
unsigned int debounce =       20; // ms debounce period to prevent flickering when pressing or releasing the button
unsigned int DCgap =         250; // max ms between clicks for a double click event
unsigned int holdTime =     1500; // ms hold period: how long to wait for press+hold event
unsigned int longHoldTime = 5000; // ms long hold period: how long to wait for press+long hold event

// Other button variables
boolean buttonVal =          HIGH; // value read from button
boolean buttonLast =         HIGH; // buffered value of the button's previous state
boolean DCwaiting =         false; // whether we're waiting for a double click (down)
boolean DConUp =            false; // whether to register a double click on next release, or whether to wait and click
boolean singleOK =           true; // whether it's OK to do a single click
long    downTime =             -1; // time the button was pressed down
long    upTime =               -1; // time the button was released
boolean ignoreUp =          false; // whether to ignore the button release because the click+hold was triggered
boolean waitForUp =         false; // when held, whether to wait for the up event
boolean holdEventPast =     false; // whether or not the hold event happened already
boolean longHoldEventPast = false; // whether or not the long hold event happened already


int ButtonReadEvent() {
  int event = 0;
  // Read the state of the button
  buttonVal = digitalRead(BUTTON_PIN);
  // Button pressed down
  if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > debounce) {
    downTime = millis();
    ignoreUp = false;
    waitForUp = false;
    singleOK = true;
    holdEventPast = false;
    longHoldEventPast = false;
    if ((millis() - upTime) < DCgap && DConUp == false && DCwaiting == true) DConUp = true;
    else DConUp = false;
    DCwaiting = false;
  }
  // Button released
  else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > debounce) {
    if (not ignoreUp) {
      upTime = millis();
      if (DConUp == false) DCwaiting = true;
      else {
        event = 2;
        DConUp = false;
        DCwaiting = false;
        singleOK = false;
        eepromOutdated = true;
      }
    }
  }
  // Test for normal click event: DCgap expired
  if ( buttonVal == HIGH && (millis() - upTime) >= DCgap && DCwaiting == true && DConUp == false && singleOK == true) {
    event = 1;
    DCwaiting = false;
    eepromOutdated = true;
  }
  // Test for hold
  if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
    // Trigger "normal" hold
    if (not holdEventPast) {
      event = 3;
      waitForUp = true;
      ignoreUp = true;
      DConUp = false;
      DCwaiting = false;
      //downTime = millis();
      holdEventPast = true;
      eepromOutdated = true;
    }
  }
  buttonLast = buttonVal;
  return event;
}
