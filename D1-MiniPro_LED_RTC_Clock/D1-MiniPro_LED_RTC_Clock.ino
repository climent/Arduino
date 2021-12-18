// #include <EEPROM.h>

// void setup()
// {
//   Serial.begin(115200);
//   delay(2000);

//   uint addr = 0;

//   // fake data
//   struct { 
//     uint val = 0;
//     char str[20] = "";
//   } data;

//   // commit 512 bytes of ESP8266 flash (for "EEPROM" emulation)
//   // this step actually loads the content (512 bytes) of flash into 
//   // a 512-byte-array cache in RAM
//   EEPROM.begin(512);

//   // read bytes (i.e. sizeof(data) from "EEPROM"),
//   // in reality, reads from byte-array cache
//   // cast bytes into structure called data
//   EEPROM.get(addr,data);
//   Serial.println("Old values are: "+String(data.val)+","+String(data.str));

//   // fiddle with the data "read" from EEPROM
//   data.val += 5;
//   if ( strcmp(data.str,"hello") == 0 )
//       strncpy(data.str, "jerry",20);
//   else 
//       strncpy(data.str, "hello",20);

//   // replace values in byte-array cache with modified data
//   // no changes made to flash, all in local byte-array cache
//   EEPROM.put(addr,data);

//   // actually write the content of byte-array cache to
//   // hardware flash.  flash write occurs if and only if one or more byte
//   // in byte-array cache has been changed, but if so, ALL 512 bytes are 
//   // written to flash
//   EEPROM.commit();  

//   // clear 'data' structure
//   data.val = 0; 
//   strncpy(data.str,"",20);

//   // reload data for EEPROM, see the change
//   //   OOPS, not actually reading flash, but reading byte-array cache (in RAM), 
//   //   power cycle ESP8266 to really see the flash/"EEPROM" updated
//   EEPROM.get(addr,data);
//   Serial.println("New values are: "+String(data.val)+","+String(data.str));
// }

// void loop()
// {
//   delay(1000);
// }
// https://learn.adafruit.com/14-segment-alpha-numeric-led-featherwing/usage
// https://github.com/adafruit/Adafruit_LED_Backpack/blob/master/Adafruit_LEDBackpack.cpp

#include <TimeLib.h>
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

#include <RtcDS3231.h>
RtcDS3231<TwoWire> Rtc(Wire);
#define countof(a) (sizeof(a) / sizeof(a[0]))

#ifndef _BV
  #define _BV(bit) (1<<(bit))
#endif

#define BUTTON_PIN 0

Adafruit_AlphaNum4 matrix = Adafruit_AlphaNum4();

#include "config.h"
#ifndef CONFIG_H
char ssid[] = "network";
char pass[] = "password";
#endif

int hours =         0;
int mins  =         0;
int secs  =         0;
int old_secs  =     0;
int delay_time =   60; // secs, update NTP time delay

bool print_secs = false;
bool first_request = true;

int brightness[2] = {1, 15};
int brightness_level = 1;

int summertimedelta;

WiFiUDP udp;
NTPClient timeClient(udp);

// EEPROM 
int DST_eeaddr =  0;
uint DST_value =  0;
int TZ_eeaddr =   1;
uint TZ_value =   0;

bool eeprom_needs_write = false;

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
  
  // Real Time Clock module
  Rtc.Begin();
  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone); 

  // RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  // printDateTime(compiled);
  // Serial.println();
  if (!Rtc.GetIsRunning())
  {
      Serial.println("RTC was not actively running, starting now");
      Rtc.SetIsRunning(true);
  }

  // EEPROM
  EEPROM.begin(512);

  // Matrix
  matrix.begin(0x70);  // pass in the address
  // matrix.setBrightness(8);

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

  // EEPROM initialization
  EEPROM.get(DST_eeaddr, DST_value);
  delay(500);
  if (DST_value != 0 && DST_value != 1) {
    Serial.print("EEPROM DST_value is: ");
    Serial.println(DST_value);
    Serial.println("Initializing EEPROM DST_value to 0 / winter.");
    DST_value = 0;
    eeprom_needs_write = true;
  } else {
    if (DST_value == 0) { Serial.println("EEPROM DST_value is 0 / winter");}
    if (DST_value == 1) { Serial.println("EEPROM DST_value is 1 / summer");}
  }
  EEPROM.get(TZ_eeaddr, TZ_value);
  Serial.print("EEPROM TZ_value is: ");
  Serial.println(TZ_value);
  if (TZ_value < 0 || TZ_value > 23) { 
    Serial.println("Initializing EEPROM TZ_value to 19 (New_York).");
    TZ_value = 19;
    eeprom_needs_write = true;
  }
}

void loop()
{
  // Blink the dot at the seconds
  now_time = millis();

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
      eeprom_needs_write = true;
			break;
    case 3:
      // TZ_value++;
      // For now use only Dublin (0) and New York (19)
      TZ_value == 19 ? TZ_value = 0 : TZ_value = 19;
      if (TZ_value > 23) { TZ_value = 0; }
      Serial.print("TZ value changed to ");
      Serial.println(TZ_value);
      eeprom_needs_write = true;
      break;
	}
  DST_value == 1 ?  summertimedelta = 1 : summertimedelta = 0;

  //Request to NIST server should be done every $delay_time interval.
  if (now_time - last_ntp_received > delay_time * 1000 || initial_request == true) {

    if (timeClient.update() || initial_request == true) {
      initial_request = false;
      last_ntp_received = millis();
      epoch = timeClient.getEpochTime();

      // If using an RTC module, save the time.
      time_t t = epoch;
      // Serial.println("GMT time as received from NTP:");
      char d_mon_yr[12];
      // get month, day and year 
      snprintf_P(d_mon_yr, countof(d_mon_yr), PSTR("%s %02u %04u"), monthShortStr(month(t)), day(t), year(t));
      // Serial.println(d_mon_yr);
      char tim_set[9];
      // get time
      snprintf_P(tim_set, countof(tim_set), PSTR("%02u:%02u:%02u"), hour(t), minute(t), second(t));
      // Serial.println(tim_set);
      Serial.println("==============================================");
      Serial.println("Setting RTC with GMT time as received from NTP");
      RtcDateTime compiled = RtcDateTime(d_mon_yr, tim_set);
      printDateTime(compiled);
      Serial.println("");
      Rtc.SetDateTime(compiled);
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
  RtcDateTime now_t = Rtc.GetDateTime();
  hours = now_t.Hour();
  mins = now_t.Minute();
  secs = now_t.Second();

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

  if (eeprom_needs_write) {
    // Writing and reading from the EEPROM does not work in wemos?
    // updateEEPROM();
    eeprom_needs_write = false;
  }
}

// HELPER FUNCTIONS ============================================================

// SERIAL ======================================================================

void printDateTime(const RtcDateTime& dt){
    char datestring[20];
    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
            dt.Year(),
            dt.Month(),
            dt.Day(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
}

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

// EEPROM ======================================================================

void updateEEPROM()
{
  // If we push the DST button, change the DST_value and save it on our eeprom
  // When we read the UTC time, we offset the output based on the time of the
  // year (that is, if we are in DST).
  Serial.print("Writing DST value in EEPROM: ");
  Serial.println(DST_value);
  EEPROM.put(DST_eeaddr, DST_value);
  EEPROM.put(TZ_eeaddr, TZ_value);

  EEPROM.commit();

  // EEPROM.get(DST_eeaddr, DST_value);
  // EEPROM.get(TZ_eeaddr, TZ_value);
  
  Serial.print("DST value in EEPROM is: ");
  if (DST_value == 0 || DST_value == 1) {
    if (DST_value == 0) Serial.println("0 / Winter");
    if (DST_value == 1) Serial.println("1 / Summer");
  } else {
    Serial.println(DST_value);
  }
  Serial.print("TZ value in EEPROM is: ");
  Serial.println(TZ_value);
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
