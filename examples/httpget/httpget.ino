/*
 * This is an example to show how to use HTTP GET with GPRSbee
 * We're doing a GET of the SODAQ time service to set the RTC
 * of the SODAQ board.
 *
 * If you want to analyse the AT commands sent to the GPRSbee
 * you can connect a UART to the D4/D5 grove of the SODAQ board.
 *
 * Note. After uploading the program to the SODAQ board you must
 * disconnect the USB, because the GPRSbee uses the same RX/TX
 * of the MCU. It will disrupt the GPRSbee connection.
 */

#define APN "internet.access.nl"
#define TIMEURL "http://time.sodaq.net/"

#include <Sodaq_DS3231.h>
#include <GPRSbee.h>
#include <Wire.h>

// Our own libraries
#include "Diag.h"

#define GPRSBEE_PWRPIN  7
#define XBEECTS_PIN     8

// Only needed if DIAG is enabled
#define DIAGPORT_RX     4
#define DIAGPORT_TX     5

//#########       diag      #############
#ifdef ENABLE_DIAG
#if defined(UBRRH) || defined(UBRR0H)
// There probably is no other Serial port that we can use
// Use a Software Serial instead
#include <SoftwareSerial.h>
SoftwareSerial diagport(DIAGPORT_RX, DIAGPORT_TX);
#else
#define diagport Serial;
#endif
#endif

//######### forward declare #############
void syncRTCwithServer();

void setup()
{
  Serial.begin(19200);          // Serial is connected to SIM900 GPRSbee
  gprsbee.init(Serial, XBEECTS_PIN, GPRSBEE_PWRPIN);

#ifdef ENABLE_DIAG
  diagport.begin(9600);
  gprsbee.setDiag(diagport);
#endif
  DIAGPRINTLN(F("Program start"));
  Serial.println(F("Program start"));

  // Make sure the GPRSbee is switched off
  gprsbee.off();
  Serial.println(F("Please disconnect USB"));
  delay(4000);

  syncRTCwithServer();
}

void loop()
{
    // Do nothing.
}

void syncRTCwithServer()
{
  char buffer[20];
  if (gprsbee.doHTTPGET(APN, TIMEURL, buffer, sizeof(buffer))) {
    DIAGPRINT("HTTP GET: "); DIAGPRINTLN(buffer);
    char *ptr;
    uint32_t newTs = strtoul(buffer, &ptr, 0);
    // Tweak the timestamp a little because doHTTPGET took a few second
    // to close the connection after getting the time from the server
    newTs += 3;
    if (ptr != buffer) {
      uint32_t oldTs = rtc.now().getEpoch();
      int32_t diffTs = abs(newTs - oldTs);
      if (diffTs > 30) {
        DIAGPRINT("Updating RTC, old=");
        DIAGPRINT(oldTs);
        DIAGPRINT(" new=");
        DIAGPRINTLN(newTs);
        rtc.setEpoch(newTs);
      }
    }
  }
}
