/*
 * Copyright (c) 2013 Kees Bakker.  All rights reserved.
 *
 * This file is part of GPRSbee.
 *
 * GPRSbee is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published bythe Free Software Foundation, either version 3 of
 * the License, or(at your option) any later version.
 *
 * GPRSbee is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GPRSbee.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

/*
 * This is an example to show the working of GPRSbee and the connection
 * with a TCP server.  In this case we're connecting to Weather Underground
 * to upload data of a Personal Weather Station (PWS).
 *
 * The core code of the TCP connection is like this:
 *    gprsbee.openTCP(APN, SERVER, PORTNUM)
 *    gprsbee.sendDataTCP(...)
 *    gprsbee.receiveLineTCP(...)
 *    gprsbee.closeTCP()
 *
 * The PWS upload is done as a HTTP GET. All the upload details are in the
 * URL.  Have a look at sendPWS(), it shows how the URL is built.
 *
 * When the program starts it will ask the user for some details about the
 * PWS.  Connect a terminal program and hit enter.  You'll see something
 * like this:

Current settings:
  PWS_ID: 
  PWS_PASSWORD: 
  timestamp (UTC): 1382376650
  B interval: 0
> 
 * Now you can fill in configuration items:
 *  id=some pws id, eg. id=ICITY68
 *  pw=password,
 *  ts=timestamp, enter the timestamp in UTC seconds since 1970-01-01 (Unix epoch)
 *  tb=number, eg. tb=300 to set a 5 minute interval for upload
 * Here is an example what it will look like afterwards

Current settings:
  PWS_ID: ICITY68
  PWS_PASSWORD: my-password
  timestamp (UTC): 1382376650
  B interval: 300
> 

 * Finally enter ok and the program starts.
 */

#include <Arduino.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>

#include <Adafruit_BMP085.h>
#include <SHT2x.h>
#include <DS3231.h>
#include <SoftwareSerial.h>
#include <dataflash.h>
#include <Wire.h>
#include <GPRSbee.h>

#include "MyDS3231.h"

#define CONFIG_EEPROM_ADDRESS   0x200
#define APN "internet.access.nl"
#define SERVER "weatherstation.wunderground.com"
#define PORTNUM 80

Adafruit_BMP085 bmp;

uint32_t myTimeStamp;
uint32_t triggerTimeB;

char timeA_counter;
boolean doTimeA;
boolean doTimeB;

//#########   pin definitions   ########
#define GPRSBEE_PWRPIN  8
#define XBEECTS_PIN     9

#define DIAGPORT_RX     10
#define DIAGPORT_TX     11


//######### modifiable settings ########
struct eeprom_config_t
{
  char pws_id[16];
  char pws_password[16];
  uint32_t timeB_interval;
} config;

//#########   diagnostic    #############
// Uncomment or make it an #udef to disable diagnostic output
#define ENABLE_DIAG     1

#ifdef ENABLE_DIAG
SoftwareSerial diagport(DIAGPORT_RX, DIAGPORT_TX);
#define DIAGPRINT(...)          diagport.print(__VA_ARGS__)
#define DIAGPRINTLN(...)        diagport.println(__VA_ARGS__)
#else
#define DIAGPRINT(...)
#define DIAGPRINTLN(...)
#endif


//######### forward declare #############
void helloTimeA();
void timeA_action();
void helloTimeB();
void timeB_action();
void setupWatchdog();
void getSettings();
void readConfig();
void updateConfig();
void sendPWS(const char *server, const char *url);

//#########    setup        #############
void setup()
{
  Serial1.begin(9600);      // Serial1 is connected to SIM900 GPRSbee
  gprsbee.init(Serial1, XBEECTS_PIN, GPRSBEE_PWRPIN);
#ifdef ENABLE_DIAG
  diagport.begin(9600);
  gprsbee.setDiag(diagport);
#endif

  // Make sure the GPRSbee is switched off
  gprsbee.off();

  bmp.begin();
  setupDS3231();
  getSettings();

  setupWatchdog();

  interrupts();

  triggerTimeB = getTSDS3231() + 10;        // This will trigger timeB action right away
}

void loop()
{
  if (doTimeA) {
    timeA_action();

    if (doTimeB) {
      timeB_action();
    }
  }
}

void helloTimeA()
{
  static int print_counter;
  static uint32_t counter;
  ++counter;
  if (++print_counter >= 10) {
    DIAGPRINT("timeA "); DIAGPRINTLN(counter);
    print_counter = 0;
  }
}

void timeA_action()
{
  doTimeA = false;
  helloTimeA();

  myTimeStamp = getTSDS3231();
  if (myTimeStamp >= triggerTimeB) {
    doTimeB = true;
  }

  // Do some work
  // ...
}

void helloTimeB()
{
  static uint32_t counter;
  ++counter;
  DIAGPRINT(" timeB "); DIAGPRINTLN(counter);
}

// Collect sensor data
void timeB_action()
{
  doTimeB = false;
  triggerTimeB += config.timeB_interval;
  helloTimeB();

  // Read temperature and pressure from BMP085
  float temp = bmp.readTemperature();           // in Celsius
  float pres = (float)bmp.readPressure() / 100; // in hPa

  // Wunderground wants temperature in Fahrenheit and pressure in inches, hmm, what's wrong with the metric system?
  temp = temp * 1.8 + 32;
  pres = pres / 33.8638866667;

  char url[200];
  char *ptr = url;
  strcpy(ptr, "/weatherstation/updateweatherstation.php?ID=");
  strcat(ptr, config.pws_id);
  strcat(ptr, "&PASSWORD=");
  strcat(ptr, config.pws_password);
  strcat(ptr, "&dateutc=");
  ptr += strlen(ptr);
  getNowUrlEscaped(ptr);
  strcat(ptr, "&baromin=");
  ptr += strlen(ptr);
  dtostrf(pres, -1, 3, ptr);
#if 1
  strcat(ptr, "&indoortempf=");
  ptr += strlen(ptr);
  dtostrf(temp, -1, 1, ptr);
#endif
  DIAGPRINTLN(url);

  // Send it via HTTP GET to server
  if (gprsbee.openTCP(APN, SERVER, PORTNUM)) {
    sendPWS(SERVER, url);
    gprsbee.closeTCP();
  }
}

//################ watchdog timer ################
// The watchdog timer is used to make timed interrupts at 60ms interval
void setupWatchdog()
{
  // start timed sequence
  WDTCSR |= (1<<WDCE) | (1<<WDE);
  // set new watchdog timeout value
  WDTCSR = (1<<WDCE) | WDTO_60MS;

  WDTCSR |= _BV(WDIE);                  // Enable WDT interrupt
}

//################ interrupt ################
ISR(WDT_vect)
{
  wdt_reset();

  // This gives us an interval for timeA of about 15*60ms = 1 second
  if (++timeA_counter >= 15) {
    timeA_counter = 0;
    doTimeA = true;
  }
}

//################  config   ################
void readConfig()
{
  struct eeprom_config_t tmp;
  eeprom_read_block((void *)&tmp, (const void *)CONFIG_EEPROM_ADDRESS, sizeof(tmp));
  // Only do a simple verification. A CRC16 checksum would be better.
  if (tmp.pws_id[0] >= ' ' && tmp.pws_id[0] <= 0x7F) {
    strcpy(config.pws_id, tmp.pws_id);
  }
  if (tmp.pws_password[0] >= ' ' && tmp.pws_password[0] <= 0x7F) {
    strcpy(config.pws_password, tmp.pws_password);
  }
  if (tmp.timeB_interval > 0 && tmp.timeB_interval <= 3600UL*24*30) {
    config.timeB_interval = tmp.timeB_interval;
  }
}

void updateConfig()
{
  eeprom_write_block((const void *)&config, (void *)CONFIG_EEPROM_ADDRESS, sizeof(config));
}

static inline bool isTimedOut(uint32_t ts)
{
  return (long)(millis() - ts) >= 0;
}

// Read a line of input. Must be terminated with <CR> and optional <LF>
void readLine(char *line, size_t size)
{
  int c;
  size_t len = 0;
  uint32_t ts_waitLF = 0;
  bool seenCR = false;

  while (1) {
    c = Serial.read();
    if (c < 0) {
      if (seenCR && isTimedOut(ts_waitLF)) {
        // Line ended with just <CR>. That's OK too.
        goto end;
      }
      continue;
    }
    if (c != '\r') {
      // Echo the input, but skip <CR>
      Serial.print((char)c);
    }

    if (c == '\r') {
      seenCR = true;
      ts_waitLF = millis() + 500;       // Wait another .5 sec for an optional LF
    } else if (c == '\n') {
      goto end;
    } else {
      // Any other character is stored in the line buffer
      if (len < size - 1) {
        *line++ = c;
        len++;
      }
    }
  }
end:
  *line = '\0';
}

struct setting_t
{
  const char *name;
  const char *cmd_prefix;
  void *value;
  void (*set_func)(struct setting_t *s, const char *line);
  void (*show_func)(struct setting_t *s);
  bool config;
};

void set_string(struct setting_t *s, const char *line)
{
  char *ptr = (char *)s->value;
  if (ptr) {
    strcpy(ptr, line);
  }
}

void set_uint16(struct setting_t *s, const char *line)
{
  uint16_t *ptr = (uint16_t *)s->value;
  if (ptr) {
    *ptr = strtoul(line, NULL, 0);
  }
}

void set_uint32(struct setting_t *s, const char *line)
{
  uint32_t *ptr = (uint32_t *)s->value;
  if (ptr) {
    *ptr = strtoul(line, NULL, 0);
  }
}

void show_name(struct setting_t *s)
{
  char *ptr = (char *)s->value;
  if (ptr) {
    Serial.print("  ");
    Serial.print(s->name);
    Serial.print(": ");
  }
}

void show_string(struct setting_t *s)
{
  char *ptr = (char *)s->value;
  if (ptr) {
    show_name(s);
    Serial.println(ptr);
  }
}

void show_uint16(struct setting_t *s)
{
  uint16_t *ptr = (uint16_t *)s->value;
  if (ptr) {
    show_name(s);
    Serial.println(*ptr);
  }
}

void show_uint32(struct setting_t *s)
{
  uint32_t *ptr = (uint32_t *)s->value;
  if (ptr) {
    show_name(s);
    Serial.println(*ptr);
  }
}

struct setting_t settings[] = {
    {"PWS_ID",          "id=", config.pws_id,          set_string, show_string, true},
    {"PWS_PASSWORD",    "pw=", config.pws_password,    set_string, show_string, true},
    {"timestamp (UTC)", "ts=", &myTimeStamp,           set_uint32, show_uint32},
    {"B interval",      "tb=", &config.timeB_interval, set_uint32, show_uint32, true},
};

void showSettings()
{
  Serial.println("Current settings:");
  for (size_t i = 0; i < sizeof(settings)/sizeof(settings[0]); ++i) {
    struct setting_t *s = &settings[i];
    if (s->show_func) {
      s->show_func(s);
    }
  }
}

int findSetting(const char *line)
{
  for (size_t i = 0; i < sizeof(settings)/sizeof(settings[0]); ++i) {
    struct setting_t *s = &settings[i];
    if (strncasecmp(line, s->cmd_prefix, strlen(s->cmd_prefix)) == 0) {
      return i;
    }
  }
  return -1;
}

// Read commands from Serial (the default Arduino serial port)
void getSettings()
{
  char line[60];
  bool done = false;
  bool need_config_update = false;
  int ix;

  myTimeStamp = getTSDS3231();
  // Read setting from EEPROM
  readConfig();

  while (!done) {
    // Show current settings
    showSettings();

    Serial.print("> ");
    readLine(line, sizeof(line));
    if (*line == '\0') {
      continue;
    }
    ix = findSetting(line);
    if (ix >= 0) {
      struct setting_t *s = &settings[ix];
      if (s->set_func) {
        s->set_func(s, line + strlen(s->cmd_prefix));
        if (s->config) {
          need_config_update = true;
        }
        if (s->value == &myTimeStamp) {
          setRTC(myTimeStamp);
        }
      }
      continue;
    }
    if (strcasecmp(line, "ok") == 0) {
      break;
    }
  }
  if (need_config_update) {
    updateConfig();
  }
}

void sendPWS(const char *server, const char *url)
{
  char buffer[250];                     // !!!! Beware stack overflow!
  char *ptr;
  int len;

  // Prepare the whole GET
  ptr = buffer;
  strcpy(ptr, "GET ");
  strcat(ptr, url);
  strcat(ptr, " HTTP/1.1\r\n");
  strcat(ptr, "User-Agent: sodaq\r\n");
  strcat(ptr, "Host: ");
  strcat(ptr, server);
  strcat(ptr, "\r\n");
  strcat(ptr, "Accept: *" "/" "*\r\n");    // Arduino IDE gets confused about * / * in string, grrr.
  strcat(ptr, "\r\n");
  len = strlen(buffer);
  DIAGPRINT(">> GET length: "); DIAGPRINTLN(len);
  DIAGPRINT(buffer);
  if (!gprsbee.sendDataTCP((uint8_t *)buffer, len)) {
    DIAGPRINTLN("sendDataTCP failed!");
  }

  while (gprsbee.receiveLineTCP(&ptr, 4000)) {
    // Ignore the result
  }
}
