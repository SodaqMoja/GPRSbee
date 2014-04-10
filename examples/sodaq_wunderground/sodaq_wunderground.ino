/*
 * Copyright (c) 2013-2014 Kees Bakker.  All rights reserved.
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

//################ includes ################
#include <avr/sleep.h>
#include <avr/wdt.h>

// Sketchbook libraries
#include <Arduino.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>

#include <Sodaq_BMP085.h>
#include <Sodaq_SHT2x.h>
#include <Sodaq_DS3231.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <GPRSbee.h>
#include <RTCTimer.h>

#include "pindefs.h"
#include "SQ_Diag.h"
#include "SQ_Utils.h"
#include "SQ_StartupCommands.h"
#include "Config.h"
#include "MyWatchdog.h"

//############ time service ################
#define TIMEURL "http://time.sodaq.net/"

static Sodaq_BMP085 bmp;

static RTCTimer timer;

// This flag is used to skip first gprsbee.off() check after an upload
static bool skipFirst;

//#########   GPRSbee Serial    #############
// Which serial port is connected to the GPRSbee?
#define gprsport        Serial

//######### forward declare #############

static void systemSleep();

static uint32_t getNow();

static void timeB_action(uint32_t now);
void setupWatchdog();
static void addNowUrlEscaped(String & str);
static void syncRTCwithServer(uint32_t now);

static bool checkConfig();

//######### correct the things we hate from Arduino #############

#undef abs

//#########    setup        #############
void setup()
{
  /* Clear WDRF in MCUSR */
  MCUSR &= ~_BV(WDRF);

  pinMode(GROVEPWR_PIN, OUTPUT);
  digitalWrite(GROVEPWR_PIN, GROVEPWR_OFF);

  gprsport.begin(9600);
  gprsbee.init(gprsport, XBEECTS_PIN, XBEEDTR_PIN);
#if ENABLE_DIAG
  diagport.begin(9600);
#if ENABLE_GPRSBEE_DIAG
  gprsbee.setDiag(diagport);
#endif
#endif
  DIAGPRINTLN(F("SODAQ wunderground"));

  // Make sure the GPRSbee is switched off
  gprsbee.off();

  Wire.begin();         // bmp.begin() does it too, but hey it won't hurt
  bmp.begin();
  rtc.begin();

  parms.read();

  do {
    startupCommands(Serial);
  } while (!checkConfig());
  parms.dump();
  parms.commit();

  // Instruct the RTCTimer how to get the "now" timestamp.
  timer.setNowCallback(getNow);

  timer.every(parms.getB(), timeB_action);

  if (parms.getS() > 10 * 60) {
    // Do an early RTC sync, so that we don't have to wait 24 hours
    timer.every(2L * 60, syncRTCwithServer, 1);
  }
  timer.every(parms.getS(), syncRTCwithServer);

#if 0
  // Do the Time B action right away (once)
  timer.every(3, timeB_action, 1);
#endif

  setupWatchdog();

  interrupts();
}

void loop()
{
  if (hz_flag) {
    hz_flag = false;
    wdt_reset();
    WDTCSR |= _BV(WDIE);
  }

  timer.update();

  systemSleep();
}

/*
 * Return the current timestamp
 *
 * This is a wrapper function to be used for the "now" callback
 * of the RTCTimer.
 */
static uint32_t getNow()
{
  return rtc.now().getEpoch();
}

static void addFloatToString(String & str, float val, char width, unsigned char precision)
{
  char buffer[20];
  dtostrf(val, width, precision, buffer);
  str += buffer;
}

/*
 * Add a string from PROGMEM to a String
 */
static void addProgmemStringToString(String & str, PGM_P src)
{
  char c;
  while ((c = pgm_read_byte(src++)) != '\0') {
    str += c;
  }
}

// Collect sensor data
static void timeB_action(uint32_t now)
{
  // Read temperature and pressure from BMP085
  float temp = bmp.readTemperature();           // in Celsius
  float pres = (float)bmp.readPressure() / 100; // in hPa

  // Wunderground wants temperature in Fahrenheit and pressure in inches,
  // hmm, what's wrong with the metric system?
  temp = temp * 1.8 + 32;
  pres = pres / 33.8638866667;

  char buffer[20];
  String url;
  if (!url.reserve(250)) {
    return;
  }
  addProgmemStringToString(url, PSTR("http://"));
  url += parms.getPWSserver();
  addProgmemStringToString(url, PSTR("/weatherstation/updateweatherstation.php?ID="));
  url += parms.getPWSid();
  addProgmemStringToString(url, PSTR("&PASSWORD="));
  url += parms.getPWSpassword();
  addProgmemStringToString(url, PSTR("&dateutc="));
  addNowUrlEscaped(url);
  addProgmemStringToString(url, PSTR("&baromin="));
  addFloatToString(url, pres, -1, 3);
  addProgmemStringToString(url, PSTR("&indoortempf="));
  addFloatToString(url, temp, -1, 1);
  DIAGPRINTLN(url);
  if (gprsbee.doHTTPGET(parms.getAPN(), url.c_str(), buffer, sizeof(buffer))) {
    // Should we check for "success"?
  }
}

/*
 * \brief Get the date and time in URL escaped format (':' => %3A, etc)
 */
static void addNowUrlEscaped(String & str)
{
  DateTime dt = rtc.now();

  add04d(str, dt.year());
  str += '-';
  add02d(str, dt.month());
  str += '-';
  add02d(str, dt.date());
  str += '+';
  add02d(str, dt.hour());
  str += "%3A";
  add02d(str, dt.minute());
  str += "%3A";
  add02d(str, dt.second());
}

static void doCheckGprsOff(uint32_t now)
{
  //DIAGPRINT(F("XBEECTS_PIN: ")); DIAGPRINTLN(digitalRead(XBEECTS_PIN));
  if (skipFirst) {
    // Skip first time after upload
    skipFirst = false;
    return;
  }
  gprsbee.off();
}

//######### watchdog and system sleep #############
static void systemSleep()
{
  ADCSRA &= ~_BV(ADEN);         // ADC disabled

  /*
  * Possible sleep modes are (see sleep.h):
  #define SLEEP_MODE_IDLE         (0)
  #define SLEEP_MODE_ADC          _BV(SM0)
  #define SLEEP_MODE_PWR_DOWN     _BV(SM1)
  #define SLEEP_MODE_PWR_SAVE     (_BV(SM0) | _BV(SM1))
  #define SLEEP_MODE_STANDBY      (_BV(SM1) | _BV(SM2))
  #define SLEEP_MODE_EXT_STANDBY  (_BV(SM0) | _BV(SM1) | _BV(SM2))
  */
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_mode();

  ADCSRA |= _BV(ADEN);          // ADC enabled
}

//################ RTC ################
/*
 * Synchronize RTC with a time server
 */
static void syncRTCwithServer(uint32_t now)
{
  //DIAGPRINT(F("syncRTCwithServer ")); DIAGPRINTLN(now);

  // If the sync does not then retry in 45 minutes or so
  // But if the sync succeeds, the next sync will be done at the usual interval
  char buffer[20];
  if (gprsbee.doHTTPGET(parms.getAPN(), TIMEURL, buffer, sizeof(buffer))) {
    //DIAGPRINT(F("HTTP GET: ")); DIAGPRINTLN(buffer);
    uint32_t newTs;
    if (getUValue(buffer, &newTs)) {
      // Tweak the timestamp a little because doHTTPGET took a few seconds
      // to close the connection after getting the time from the server
      newTs += 3;
      uint32_t oldTs = rtc.now().getEpoch();
      int32_t diffTs = abs(newTs - oldTs);
      if (diffTs > 30) {
        DIAGPRINT(F("Updating RTC, old=")); DIAGPRINT(oldTs);
        DIAGPRINT(F(" new=")); DIAGPRINTLN(newTs);
        timer.adjust(oldTs, newTs);
        rtc.setEpoch(newTs);
      }
      goto end;
    }
  }

  // Sync failed.
  // ???? Retry in, say, 3 minutes
  //timer.every(5L * 60, syncRTCwithServer, 1);
  showFreeRAM();
end:
  ;
}

/*
 * Check if all required config parameters are filled in
 */
static bool checkConfig()
{
  if (!parms.checkConfig()) {
    return false;
  }
  return true;
}
