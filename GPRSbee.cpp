/*
 * Copyright (c) 2013 Kees Bakker.  All rights reserved.
 *
 * This file is part of GPRSbee.
 *
 * GPRSbee is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
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

#include <Arduino.h>
#include <Stream.h>
#include <avr/wdt.h>

#include "GPRSbee.h"

#if ENABLE_GPRSBEE_DIAG
#define diagPrint(...) { if (_diagStream) _diagStream->print(__VA_ARGS__); }
#define diagPrintLn(...) { if (_diagStream) _diagStream->println(__VA_ARGS__); }
#else
#define diagPrint(...)
#define diagPrintLn(...)
#endif


GPRSbeeClass gprsbee;

/*
 * A wrapper for delay that also resets the WDT while waiting
 */
static inline void mydelay(unsigned long nrMillis)
{
  while (nrMillis > 100) {
    wdt_reset();
    delay(100);
    nrMillis -= 100;
  }
  delay(nrMillis);
}

void GPRSbeeClass::init(Stream &stream, int ctsPin, int powerPin)
{
  _myStream = &stream;
  _diagStream = 0;
  _ctsPin = ctsPin;
  _powerPin = powerPin;
  _minSignalQuality = 10;
  _ftpMaxLength = 0;
  _transMode = false;
  _echoOff = false;

  digitalWrite(_powerPin, LOW);
  pinMode(_powerPin, OUTPUT);

  pinMode(_ctsPin, INPUT);
}

bool GPRSbeeClass::on()
{
  if (!isOn()) {
    toggle();
  }
  // Make sure it responds
  if (!isAlive()) {
    // Oh, no answer, maybe it's off
    // Fall through and rely on the cts pin
  }
  return isOn();
}


bool GPRSbeeClass::off()
{
  if (isOn()) {
    toggle();
    // There should be a message "NORMAL POWER DOWN"
    // Shall we wait for it?
    uint32_t ts_max = millis() + 4000;
    if (waitForMessage_P(PSTR("NORMAL POWER DOWN"), ts_max)) {
      // OK. The SIM900 is switched off
    } else {
      // Should we care if it didn't?
    }
  }
  _echoOff = false;
  return !isOn();
}

bool GPRSbeeClass::isOn()
{
  bool XBEE_cts = digitalRead(_ctsPin);
  return XBEE_cts;
}

void GPRSbeeClass::toggle()
{
#if 1
  // To be on the safe side, make sure we start from LOW
  // TODO Decide if this is useful.
  digitalWrite(_powerPin, LOW);
  mydelay(1000);
#endif
  digitalWrite(_powerPin, HIGH);
  mydelay(2500);
  digitalWrite(_powerPin, LOW);
}

bool GPRSbeeClass::isAlive()
{
  // Send "AT" and wait for "OK"
  // Try it at least 3 times before deciding it failed
  for (int i = 0; i < 3; i++) {
    sendCommand_P(PSTR("AT"));
    if (waitForOK()) {
      return true;
    }
  }
  return false;
}

void GPRSbeeClass::switchEchoOff()
{
  if (!_echoOff) {
    // Suppress echoing
    if (!sendCommandWaitForOK_P(PSTR("ATE0"))) {
      return;
    }
    _echoOff = true;
  }
}

void GPRSbeeClass::flushInput()
{
  int c;
  while ((c = _myStream->read()) >= 0) {
    diagPrint((char)c);
  }
}

/*
 * \brief Read a line of input from SIM900
 */
int GPRSbeeClass::readLine(uint32_t ts_max)
{
  uint32_t ts_waitLF = 0;
  bool seenCR = false;
  int c;

  //diagPrintLn(F("readLine"));
  _SIM900_bufcnt = 0;
  while (!isTimedOut(ts_max)) {
    wdt_reset();
    if (seenCR) {
      c = _myStream->peek();
      // ts_waitLF is guaranteed to be non-zero
      if ((c == -1 && isTimedOut(ts_waitLF)) || (c != -1 && c != '\n')) {
        //diagPrint(F("readLine:  peek '")); diagPrint(c); diagPrintLn('\'');
        // Line ended with just <CR>. That's OK too.
        goto ok;
      }
      // Only \n should fall through
    }

    c = _myStream->read();
    if (c < 0) {
      continue;
    }
    diagPrint((char)c);                 // echo the char
    seenCR = c == '\r';
    if (c == '\r') {
      ts_waitLF = millis() + 50;        // Wait another .05 sec for an optional LF
    } else if (c == '\n') {
      goto ok;
    } else {
      // Any other character is stored in the line buffer
      if (_SIM900_bufcnt < SIM900_BUFLEN) {
        _SIM900_buffer[_SIM900_bufcnt++] = c;
      }
    }
  }

  diagPrintLn(F("readLine timed out"));
  return -1;            // This indicates: timed out

ok:
  _SIM900_buffer[_SIM900_bufcnt] = 0;     // Terminate with NUL byte
  //diagPrint(F(" ")); diagPrintLn(_SIM900_buffer);
  return _SIM900_bufcnt;

}

/*
 * \brief Read a number of bytes from SIM900
 *
 * Read <len> bytes from SIM900 and store at most <buflen> in the buffer.
 *
 * Return 0 if <len> bytes were read from SIM900, else return the remaining number
 * that wasn't read due to the timeout.
 * Note. The buffer is a byte buffer and not a string, and it is not terminated
 * with a NUL byte.
 */
int GPRSbeeClass::readBytes(size_t len, uint8_t *buffer, size_t buflen, uint32_t ts_max)
{
  //diagPrintLn(F("readBytes"));
  while (!isTimedOut(ts_max) && len > 0) {
    wdt_reset();
    int c = _myStream->read();
    if (c < 0) {
      continue;
    }
    // Each character is stored in the buffer
    --len;
    if (buflen > 0) {
      *buffer++ = c;
      --buflen;
    }
  }
  if (buflen > 0) {
    // This is just a convenience if the data is an ASCII string (which we don't know here).
    *buffer = 0;
  }
  return len;
}

bool GPRSbeeClass::waitForOK(uint16_t timeout)
{
  int len;
  uint32_t ts_max = millis() + timeout;
  while ((len = readLine(ts_max)) >= 0) {
    if (len == 0) {
      // Skip empty lines
      continue;
    }
    if (strcmp_P(_SIM900_buffer, PSTR("OK")) == 0) {
      return true;
    }
    // Other input is skipped.
  }
  return false;         // This indicates: timed out
}

bool GPRSbeeClass::waitForMessage(const char *msg, uint32_t ts_max)
{
  int len;
  //diagPrint(F("waitForMessage: ")); diagPrintLn(msg);
  while ((len = readLine(ts_max)) >= 0) {
    if (len == 0) {
      // Skip empty lines
      continue;
    }
    if (strncmp(_SIM900_buffer, msg, strlen(msg)) == 0) {
      return true;
    }
  }
  return false;         // This indicates: timed out
}
bool GPRSbeeClass::waitForMessage_P(const char *msg, uint32_t ts_max)
{
  int len;
  //diagPrint(F("waitForMessage: ")); diagPrintLn(msg);
  while ((len = readLine(ts_max)) >= 0) {
    if (len == 0) {
      // Skip empty lines
      continue;
    }
    if (strncmp_P(_SIM900_buffer, msg, strlen_P(msg)) == 0) {
      return true;
    }
  }
  return false;         // This indicates: timed out
}

int GPRSbeeClass::waitForMessages(PGM_P msgs[], size_t nrMsgs, uint32_t ts_max)
{
  int len;
  //diagPrint(F("waitForMessages: ")); diagPrintLn(msgs[0]);
  while ((len = readLine(ts_max)) >= 0) {
    if (len == 0) {
      // Skip empty lines
      continue;
    }
    //diagPrint(F(" checking \"")); diagPrint(_SIM900_buffer); diagPrintLn("\"");
    for (size_t i = 0; i < nrMsgs; ++i) {
      //diagPrint(F("  checking \"")); diagPrint(msgs[i]); diagPrintLn("\"");
      if (strcmp_P(_SIM900_buffer, msgs[i]) == 0) {
        //diagPrint(F("  found i=")); diagPrint((int)i); diagPrintLn("");
        return i;
      }
    }
  }
  return -1;         // This indicates: timed out
}

/*
 * \brief Wait for a prompt, or timeout
 *
 * \return true if succeeded (the reply received), false if otherwise (timed out)
 */
bool GPRSbeeClass::waitForPrompt(const char *prompt, uint32_t ts_max)
{
  const char * ptr = prompt;

  while (*ptr != '\0') {
    wdt_reset();
    if (isTimedOut(ts_max)) {
      break;
    }

    int c = _myStream->read();
    if (c < 0) {
      continue;
    }

    diagPrint((char)c);
    switch (c) {
    case '\r':
      // Ignore
      break;
    case '\n':
      // Start all over
      ptr = prompt;
      break;
    default:
      if (*ptr == c) {
        ptr++;
      } else {
        // Start all over
        ptr = prompt;
      }
      break;
    }
  }

  return true;
}

void GPRSbeeClass::sendCommandPrepare()
{
  flushInput();
  mydelay(50);
  diagPrint(F(">> "));
}
void GPRSbeeClass::sendCommandPartial(const char *cmd)
{
  diagPrint(cmd);
  _myStream->print(cmd);
}
void GPRSbeeClass::sendCommandPartial_P(const char *cmd)
{
  diagPrint(reinterpret_cast<const __FlashStringHelper *>(cmd));
  _myStream->print(reinterpret_cast<const __FlashStringHelper *>(cmd));
}
void GPRSbeeClass::sendCommandNoPrepare(const char *cmd)
{
  sendCommandPartial(cmd);
  diagPrintLn();
  _myStream->print('\r');
}
void GPRSbeeClass::sendCommandNoPrepare_P(const char *cmd)
{
  sendCommandPartial_P(cmd);
  diagPrintLn();
  _myStream->print('\r');
}
void GPRSbeeClass::sendCommand(const char *cmd)
{
  sendCommandPrepare();
  sendCommandNoPrepare(cmd);
}
void GPRSbeeClass::sendCommand_P(const char *cmd)
{
  sendCommandPrepare();
  sendCommandNoPrepare_P(cmd);
}

/*
 * \brief Send a command to the SIM900 and wait for "OK"
 *
 * The command string should not include the <CR>
 * Return true, only if "OK" is seen. "ERROR" and timeout
 * result in false.
 */
bool GPRSbeeClass::sendCommandWaitForOK(const char *cmd, uint16_t timeout)
{
  sendCommand(cmd);
  return waitForOK(timeout);
}
bool GPRSbeeClass::sendCommandWaitForOK_P(const char *cmd, uint16_t timeout)
{
  sendCommand_P(cmd);
  return waitForOK(timeout);
}

/*
 * \brief Get SIM900 integer value
 *
 * Send the SIM900 command and wait for the reply. The reply
 * also contains the value that we want. We use the first value
 * upto the comma or the end
 * Finally the SIM900 should give "OK"
 *
 * An example is:
 *   >> AT+CSQ
 *   << +CSQ: 18,0
 *   <<
 *   << OK
 */
bool GPRSbeeClass::getIntValue(const char *cmd, const char *reply, int * value, uint32_t ts_max)
{
  sendCommand(cmd);

  // First we expect the reply
  if (waitForMessage(reply, ts_max)) {
    const char *ptr = _SIM900_buffer + strlen(reply);
    char *bufend;
    *value = strtoul(ptr, &bufend, 0);
    if (bufend == ptr) {
      // Invalid number
      return false;
    }
    // Wait for "OK"
    return waitForOK();
  }
  return false;
}

/*
 * \brief Get SIM900 string value
 *
 * Send the SIM900 command and wait for the reply (prefixed with <reply>.
 * Finally the SIM900 should give "OK"
 *
 * An example is:
 *   >> AT+GCAP
 *   << +GCAP:+FCLASS,+CGSM
 *   <<
 *   << OK
 */
bool GPRSbeeClass::getStrValue(const char *cmd, const char *reply, char * str, size_t size, uint32_t ts_max)
{
  sendCommand(cmd);

  if (waitForMessage(reply, ts_max)) {
    const char *ptr = _SIM900_buffer + strlen(reply);
    strncpy(str, ptr, size - 1);
    // Wait for "OK"
    return waitForOK();
  }
  return false;
}

/*
 * \brief Get SIM900 string value
 *
 * Send the SIM900 command and wait for the reply.
 * Finally the SIM900 should give "OK"
 *
 * An example is:
 *   >> AT+GSN
 *   << 861785005921311
 *   <<
 *   << OK
 */
bool GPRSbeeClass::getStrValue(const char *cmd, char * str, size_t size, uint32_t ts_max)
{
  sendCommand(cmd);

  int len;
  while ((len = readLine(ts_max)) >= 0) {
    if (len == 0) {
      // Skip empty lines
      continue;
    }
    strncpy(str, _SIM900_buffer, size - 1);
    break;
  }
  if (len < 0) {
      // There was a timeout
      return false;
  }
  // Wait for "OK"
  return waitForOK();
}

bool GPRSbeeClass::waitForSignalQuality()
{
  // TODO This timeout is maybe too long.
  uint32_t ts_max = millis() + 120000;
  int value;
  while (!isTimedOut(ts_max)) {
    if (getIntValue("AT+CSQ", "+CSQ:", &value, millis() + 12000 )) {
      if (value >= _minSignalQuality) {
        return true;
      }
    }
    mydelay(500);
    if (!isAlive()) {
      break;
    }
  }
  return false;
}

bool GPRSbeeClass::waitForCREG()
{
  // TODO This timeout is maybe too long.
  uint32_t ts_max = millis() + 120000;
  int value;
  while (!isTimedOut(ts_max)) {
    sendCommand_P(PSTR("AT+CREG?"));
    // Reply is:
    // +CREG: <n>,<stat>[,<lac>,<ci>]   mostly this is +CREG: 0,1
    // we want the second number, the <stat>
    // 0 = Not registered, MT is not currently searching an operator to register to
    // 1 = Registered, home network
    // 2 = Not registered, but MT is currently trying to attach...
    // 3 = Registration denied
    // 4 = Unknown
    // 5 = Registered, roaming
    value = 0;
    if (waitForMessage_P(PSTR("+CREG:"), millis() + 12000)) {
      const char *ptr = strchr(_SIM900_buffer, ',');
      if (ptr) {
        ++ptr;
        value = strtoul(ptr, NULL, 0);
      }
    }
    waitForOK();
    if (value == 1 || value == 5) {
      return true;
    }
    mydelay(500);
    if (!isAlive()) {
      break;
    }
  }
  return false;
}

/*
Secondly, you should use the command group AT+CSTT, AT+CIICR and AT+CIFSR to start
the task and activate the wireless connection. Lastly, you can establish TCP connection between
SIM900 and server by AT command (AT+CIPSTART=”TCP”,”IP Address of server”, “port
number of server”). If the connection is established successfully, response “CONNECT OK” will
come up from the module. Now you can send data to server with “AT+CIPSEND”.
“AT+CIPSEND” will return with promoting mark “>”, you should write data after “>” then issue
CTRL+Z (0x1a) to send. If sending is successful, it will respond “SEND OK”. And if there is data
coming from server, the module will receive the data automatically from the serial port. You can
close the TCP connection with “AT+CIPCLOSE” command. Below is an example of TCP
connection to remote server.
 */


bool GPRSbeeClass::openTCP(const char *apn,
    const char *server, int port, bool transMode)
{
  return openTCP(apn, 0, 0, server, port, transMode);
}

bool GPRSbeeClass::openTCP(const char *apn, const char *apnuser, const char *apnpwd,
    const char *server, int port, bool transMode)
{
  uint32_t ts_max;
  boolean retval = false;
  char cmdbuf[60];              // big enough for AT+CIPSTART="TCP","server",8500
  PGM_P CIPSTART_replies[] = {
      PSTR("CONNECT OK"),
      PSTR("CONNECT"),

      PSTR("CONNECT FAIL"),
      //"STATE: TCP CLOSED",
  };
  const size_t nrReplies = sizeof(CIPSTART_replies) / sizeof(CIPSTART_replies[0]);

  if (!on()) {
    goto ending;
  }

  // Suppress echoing
  switchEchoOff();

  // Wait for signal quality
  if (!waitForSignalQuality()) {
    goto cmd_error;
  }

  // Wait for CREG
  if (!waitForCREG()) {
    goto cmd_error;
  }

  // Attach to GPRS service
  // We need a longer timeout than the normal waitForOK
  if (!sendCommandWaitForOK_P(PSTR("AT+CGATT=1"), 30000)) {
    goto cmd_error;
  }

  // AT+CSTT=<apn>,<username>,<password>
  strcpy_P(cmdbuf, PSTR("AT+CSTT=\""));
  strcat(cmdbuf, apn);
  strcat(cmdbuf, "\"");
  if (!sendCommandWaitForOK(cmdbuf)) {
    goto cmd_error;
  }

  if (!sendCommandWaitForOK_P(PSTR("AT+CIICR"))) {
    goto cmd_error;
  }

#if 0
  // Get local IP address
  if (!sendCommandWaitForOK_P(PSTR("AT+CISFR"))) {
    goto cmd_error;
  }
#endif

  // AT+CIPSHUT
  sendCommand_P(PSTR("AT+CIPSHUT"));
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage_P(PSTR("SHUT OK"), ts_max)) {
    goto cmd_error;
  }

  if (transMode) {
    if (!sendCommandWaitForOK_P(PSTR("AT+CIPMODE=1"))) {
      goto cmd_error;
    }
    //AT+CIPCCFG
    // Read the current settings
    if (!sendCommandWaitForOK_P(PSTR("AT+CIPCCFG?"))) {
      goto cmd_error;
    }
  }

  // Start up the connection
  // AT+CIPSTART="TCP","server",8500
  strcpy_P(cmdbuf, PSTR("AT+CIPSTART=\"TCP\",\""));
  strcat(cmdbuf, server);
  strcat_P(cmdbuf, PSTR("\","));
  itoa(port, cmdbuf + strlen(cmdbuf), 10);
  if (!sendCommandWaitForOK(cmdbuf)) {
    goto cmd_error;
  }
  ts_max = millis() + 15000;            // Is this enough?
  int ix;
  if ((ix = waitForMessages(CIPSTART_replies, nrReplies, ts_max)) < 0) {
    // For some weird reason the SIM900 in some cases does not want
    // to give us this CONNECT OK. But then we see it later in the stream.
    // The manual (V1.03) says that we can expect "CONNECT OK", but so far
    // we have only seen just "CONNECT" (or an error of course).
    goto cmd_error;
  }
  if (ix >= 2) {
    // Only some CIPSTART_replies are acceptable, i.e. "CONNECT" and "CONNECT OK"
    goto cmd_error;
  }

  // AT+CIPQSEND=0  normal send mode (reply after each data send will be SEND OK)
  if (false && !sendCommandWaitForOK_P(PSTR("AT+CIPQSEND=0"))) {
    goto cmd_error;
  }

  _transMode = transMode;
  retval = true;
  goto ending;

cmd_error:
  diagPrintLn(F("openTCP failed!"));
  off();

ending:
  return retval;
}

void GPRSbeeClass::closeTCP()
{
  uint32_t ts_max;
  // AT+CIPSHUT
  // Maybe we should do AT+CIPCLOSE=1
  if (_transMode) {
    mydelay(1000);
    _myStream->print(F("+++"));
    mydelay(500);
    // TODO Will the SIM900 answer with "OK"?
  }
  sendCommand_P(PSTR("AT+CIPSHUT"));
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage_P(PSTR("SHUT OK"), ts_max)) {
    diagPrintLn(F("closeTCP failed!"));
  }

  off();
}

bool GPRSbeeClass::isTCPConnected()
{
  uint32_t ts_max;
  bool retval = false;
  char *ptr;

  if (!isOn()) {
    goto end;
  }

  if (_transMode) {
    // We need to send +++
    mydelay(1000);
    _myStream->print(F("+++"));
    mydelay(500);
    if (!waitForOK()) {
      goto end;
    }
  }

  // AT+CIPSTATUS
  // Expected answer:
  // OK
  // STATE: <state>
  // The only good answer is "CONNECT OK"
  if (!sendCommandWaitForOK_P(PSTR("AT+CIPSTATUS"))) {
    goto end;
  }
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage_P(PSTR("STATE:"), ts_max)) {
    goto end;
  }
  ptr = _SIM900_buffer + 6;
  while (*ptr != '\0' && *ptr == ' ') {
    ++ptr;
  }
  // Look at the state
  if (strcmp_P(ptr, PSTR("CONNECT OK")) != 0) {
    goto end;
  }

  if (_transMode) {
    // We must switch back to transparent mode
    sendCommand_P(PSTR("ATO0"));
    // TODO wait for "CONNECT" or "NO CARRIER"
    ts_max = millis() + 4000;             // Is this enough? Or too much
    if (!waitForMessage_P(PSTR("CONNECT"), ts_max)) {
      goto end;
    }
  }

  retval = true;

end:
  return retval;
}

/*
 * \brief Send some data over the TCP connection
 */
bool GPRSbeeClass::sendDataTCP(uint8_t *data, int data_len)
{
  uint32_t ts_max;
  bool retval = false;

  mydelay(500);
  flushInput();
  _myStream->print(F("AT+CIPSEND="));
  _myStream->println(data_len);
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForPrompt("> ", ts_max)) {
    goto error;
  }
  mydelay(500);           // Wait a little, just to be sure
  // Send the data
  for (int i = 0; i < data_len; ++i) {
    _myStream->print((char)*data++);
  }
  //
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage_P(PSTR("SEND OK"), ts_max)) {
    goto error;
  }

  retval = true;
  goto ending;
error:
  diagPrintLn(F("sendDataTCP failed!"));
ending:
  return retval;
}

bool GPRSbeeClass::receiveLineTCP(const char **buffer, uint16_t timeout)
{
  uint32_t ts_max;
  bool retval = false;

  //diagPrintLn(F("receiveLineTCP"));
  *buffer = NULL;
  ts_max = millis() + timeout;
  if (readLine(ts_max) < 0) {
    goto ending;
  }
  *buffer = _SIM900_buffer;
  retval = true;

ending:
  return retval;
}

/*
 * \brief Open a (FTP) session
 */
bool GPRSbeeClass::openFTP(const char *apn,
    const char *server, const char *username, const char *password)
{
  return openFTP(apn, 0, 0, server, username, password);
}

bool GPRSbeeClass::openFTP(const char *apn, const char *apnuser, const char *apnpwd,
    const char *server, const char *username, const char *password)
{
  char cmd[64];

  if (!on()) {
    goto ending;
  }

  // Suppress echoing
  switchEchoOff();

  // Wait for signal quality
  if (!waitForSignalQuality()) {
    goto cmd_error;
  }

  // Wait for CREG
  if (!waitForCREG()) {
    goto cmd_error;
  }

  // Attach to GPRS service
  // We need a longer timeout than the normal waitForOK
  if (!sendCommandWaitForOK_P(PSTR("AT+CGATT=1"), 30000)) {
    goto cmd_error;
  }

  if (!setBearerParms(apn, apnuser, apnpwd)) {
    goto cmd_error;
  }

  if (!sendCommandWaitForOK_P(PSTR("AT+FTPCID=1"))) {
    goto cmd_error;
  }

  // connect to FTP server
  //snprintf(cmd, sizeof(cmd), "AT+FTPSERV=\"%s\"", server);
  strcpy_P(cmd, PSTR("AT+FTPSERV=\""));
  strcat(cmd, server);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto cmd_error;
  }

  // optional "AT+FTPPORT=21";
  //snprintf(cmd, sizeof(cmd), "AT+FTPUN=\"%s\"", username);
  strcpy_P(cmd, PSTR("AT+FTPUN=\""));
  strcat(cmd, username);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto cmd_error;
  }
  //snprintf(cmd, sizeof(cmd), "AT+FTPPW=\"%s\"", password);
  strcpy_P(cmd, PSTR("AT+FTPPW=\""));
  strcat(cmd, password);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto cmd_error;
  }

  return true;

cmd_error:
  diagPrintLn(F("openFTP failed!"));
  off();

ending:
  return false;
}

bool GPRSbeeClass::closeFTP()
{
  off();            // Ignore errors
  return true;
}

/*
 * \brief Open a (FTP) session (one file)
 */
bool GPRSbeeClass::openFTPfile(const char *fname, const char *path)
{
  char cmd[64];
  int retry;
  uint32_t ts_max;

  // Open FTP file
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUTNAME=\"%s\"", fname);
  strcpy_P(cmd, PSTR("AT+FTPPUTNAME=\""));
  strcat(cmd, fname);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUTPATH=\"%s\"", FTPPATH);
  strcpy_P(cmd, PSTR("AT+FTPPUTPATH=\""));
  strcat(cmd, path);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }

  // Repeat until we get OK
  for (retry = 0; retry < 5; retry++) {
    if (sendCommandWaitForOK_P(PSTR("AT+FTPPUT=1"))) {
      // +FTPPUT:1,1,1360  <= the 1360 is <maxlength>
      // +FTPPUT:1,61      <= this is an error (Net error)
      // +FTPPUT:1,66      <= this is an error (operation not allowed)
      // This can take a while ...
      ts_max = millis() + 30000;
      if (!waitForMessage_P(PSTR("+FTPPUT:1,"), ts_max)) {
        // Try again.
        isAlive();
        continue;
      }
      if (strncmp_P(_SIM900_buffer + 10, PSTR("1,"), 2) != 0) {
        // We did NOT get "+FTPPUT:1,1,", it might be an error.
        goto ending;
      }
      _ftpMaxLength = strtoul(_SIM900_buffer + 12, NULL, 0);

      break;
    }
  }
  if (retry >= 5) {
    goto ending;
  }

  return true;

ending:
  return false;
}

bool GPRSbeeClass::closeFTPfile()
{
  // Close file
  if (!sendCommandWaitForOK_P(PSTR("AT+FTPPUT=2,0"))) {
    return false;
  }

  /*
   * FIXME
   * Something weird happens here. If we wait too short (e.g. 4000)
   * then still no reply. But then when we switch off the SIM900 the
   * message +FTPPUT:1,nn message comes in, right before AT-OK or
   * +SAPBR 1: DEACT
   *
   * It is such a waste to wait that long (battery life and such).
   * The FTP file seems to be closed properly, so why bother?
   */
  // +FTPPUT:1,0
  uint32_t ts_max = millis() + 20000;
  if (!waitForMessage_P(PSTR("+FTPPUT:1,"), ts_max)) {
    // How bad is it if we ignore this
    //diagPrintLn(F("Timeout while waiting for +FTPPUT:1,"));
  }

  return true;
}

/*
 * \brief Lower layer function to insert a number of bytes in the FTP session
 *
 * The function sendBuffer() is the one to use. It takes care of splitting up
 * in chunks not bigger than maxlength
 */
bool GPRSbeeClass::sendFTPdata_low(uint8_t *buffer, size_t size)
{
  char cmd[20];         // Should be enough for "AT+FTPPUT=2,<num>"
  uint32_t ts_max;
  uint8_t *ptr = buffer;

  // Send some data
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUT=2,%d", size);
  strcpy_P(cmd, PSTR("AT+FTPPUT=2,"));
  itoa(size, cmd + strlen(cmd), 10);
  sendCommand(cmd);

  ts_max = millis() + 10000;
  // +FTPPUT:2,22
  if (!waitForMessage_P(PSTR("+FTPPUT:2,"), ts_max)) {
    // How bad is it if we ignore this
    return false;
  }
  mydelay(100);           // TODO Find out if we can drop this

  // Send data ...
  for (size_t i = 0; i < size; ++i) {
    _myStream->print((char)*ptr++);
  }
  //_myStream->print('\r');          // dummy <CR>, not sure if this is needed

  // Expected reply:
  // +FTPPUT:2,22
  // OK
  // +FTPPUT:1,1,1360

  if (!waitForOK(5000)) {
    return false;
  }

  // The SIM900 informs again what the new max length is
  ts_max = millis() + 4000;
  // +FTPPUT:1,1,1360
  if (!waitForMessage_P(PSTR("+FTPPUT:1,"), ts_max)) {
    // How bad is it if we ignore this?
    // It informs us about the _ftpMaxLength
  }

  return true;
}

bool GPRSbeeClass::sendFTPdata_low(uint8_t (*read)(), size_t size)
{
  char cmd[20];         // Should be enough for "AT+FTPPUT=2,<num>"
  uint32_t ts_max;

  // Send some data
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUT=2,%d", size);
  strcpy_P(cmd, PSTR("AT+FTPPUT=2,"));
  itoa(size, cmd + strlen(cmd), 10);
  sendCommand(cmd);

  ts_max = millis() + 10000;
  // +FTPPUT:2,22
  if (!waitForMessage_P(PSTR("+FTPPUT:2,"), ts_max)) {
    // How bad is it if we ignore this
    return false;
  }
  mydelay(100);           // TODO Find out if we can drop this

  // Send data ...
  for (size_t i = 0; i < size; ++i) {
    _myStream->print((char)(*read)());
  }

  // Expected reply:
  // +FTPPUT:2,22
  // OK
  // +FTPPUT:1,1,1360

  if (!waitForOK(5000)) {
    return false;
  }

  // The SIM900 informs again what the new max length is
  ts_max = millis() + 30000;
  // +FTPPUT:1,1,1360
  if (!waitForMessage_P(PSTR("+FTPPUT:1,"), ts_max)) {
    // How bad is it if we ignore this?
    // It informs us about the _ftpMaxLength
  }

  return true;
}

bool GPRSbeeClass::sendFTPdata(uint8_t *data, size_t size)
{
  // Send the bytes in chunks that are maximized by the maximum
  // FTP length
  while (size > 0) {
    size_t my_size = size;
    if (my_size > _ftpMaxLength) {
      my_size = _ftpMaxLength;
    }
    if (!sendFTPdata_low(data, my_size)) {
      return false;
    }
    data += my_size;
    size -= my_size;
  }
  return true;
}
bool GPRSbeeClass::sendFTPdata(uint8_t (*read)(), size_t size)
{
  // Send the bytes in chunks that are maximized by the maximum
  // FTP length
  while (size > 0) {
    size_t my_size = size;
    if (my_size > _ftpMaxLength) {
      my_size = _ftpMaxLength;
    }
    if (!sendFTPdata_low(read, my_size)) {
      return false;
    }
    size -= my_size;
  }
  return true;
}

bool GPRSbeeClass::sendSMS(const char *telno, const char *text)
{
  char cmd[64];
  uint32_t ts_max;
  bool retval = false;

  if (!on()) {
    goto ending;
  }

  // Suppress echoing
  switchEchoOff();

  // Wait for signal quality
  if (!waitForSignalQuality()) {
    goto cmd_error;
  }

  // Wait for CREG
  if (!waitForCREG()) {
    goto cmd_error;
  }

  if (!sendCommandWaitForOK_P(PSTR("AT+CMGF=1"))) {
    goto cmd_error;
  }

  strcpy_P(cmd, PSTR("AT+CMGS=\""));
  strcat(cmd, telno);
  strcat(cmd, "\"");
  sendCommand(cmd);
  ts_max = millis() + 4000;
  if (!waitForPrompt("> ", ts_max)) {
    goto cmd_error;
  }
  _myStream->print(text); //the message itself
  _myStream->print((char)26); //the ASCII code of ctrl+z is 26, this is needed to end the send modus and send the message.
  if (!waitForOK(30000)) {
    goto cmd_error;
  }

  retval = true;
  goto ending;

cmd_error:
  diagPrintLn(F("sendSMS failed!"));

ending:
  off();
  return retval;
}

bool GPRSbeeClass::doHTTPGET(const char *apn, const char *url, char *buffer, size_t len)
{
  return doHTTPGET(apn, 0, 0, url, buffer, len);
}

bool GPRSbeeClass::doHTTPGET(const char *apn, const char *apnuser, const char *apnpwd, const char *url, char *buffer, size_t len)
{
  uint32_t ts_max;
  size_t getLength = 0;
  int i;
  bool retval = false;

  if (!on()) {
    goto ending;
  }

  // Suppress echoing
  switchEchoOff();

  // Wait for signal quality
  if (!waitForSignalQuality()) {
    goto cmd_error;
  }

  // Wait for CREG
  if (!waitForCREG()) {
    goto cmd_error;
  }

  // Attach to GPRS service
  // We need a longer timeout than the normal waitForOK
  if (!sendCommandWaitForOK_P(PSTR("AT+CGATT=1"), 30000)) {
    goto cmd_error;
  }

  if (!setBearerParms(apn, apnuser, apnpwd)) {
    goto cmd_error;
  }

  // initialize http service
  if (!sendCommandWaitForOK_P(PSTR("AT+HTTPINIT"))) {
    goto cmd_error;
  }

  // set http param CID value
  // FIXME Do we need this?

  // set http param URL value
  sendCommandPrepare();
  sendCommandPartial_P(PSTR("AT+HTTPPARA=\"URL\",\""));
  sendCommandPartial(url);
  sendCommandNoPrepare_P(PSTR("\""));
  if (!waitForOK()) {
    goto cmd_error;
  }

  // set http action type 0 = GET, 1 = POST, 2 = HEAD
  if (!sendCommandWaitForOK_P(PSTR("AT+HTTPACTION=0"))) {
    goto cmd_error;
  }
  // Now we're expecting something like this: +HTTPACTION: <Method>,<StatusCode>,<DataLen>
  // <Method> 0
  // <StatusCode> 200
  // <DataLen> ??
  ts_max = millis() + 4000;
  if (waitForMessage_P(PSTR("+HTTPACTION:"), ts_max)) {
    // TODO Check for StatusCode 200
    // TODO Check for DataLen
  }

  // Read all data
  // Expect
  //   +HTTPREAD:<date_len>
  //   <data>
  //   OK
  sendCommand_P(PSTR("AT+HTTPREAD"));
  ts_max = millis() + 8000;
  if (waitForMessage_P(PSTR("+HTTPREAD:"), ts_max)) {
    const char *ptr = _SIM900_buffer + 10;
    char *bufend;
    getLength = strtoul(ptr, &bufend, 0);
    if (bufend == ptr) {
      // Invalid number
      goto cmd_error;
    }
  } else {
    // Hmm. Why didn't we get this?
    goto cmd_error;
  }
  // Read the data
  retval = true;                // assume this will succeed
  ts_max = millis() + 4000;
  i = readBytes(getLength, (uint8_t *)buffer, len, ts_max);
  if (i != 0) {
    // We didn't get the bytes that we expected
    // Still wait for OK
    retval = false;
  } else {
    // The read was successful. readBytes made sure it was terminated.
  }
  if (!waitForOK()) {
    // This is an error, but we can still return success.
    goto cmd_error;
  }

  if (!sendCommandWaitForOK_P(PSTR("AT+HTTPTERM"))) {
    // This is an error, but we can still return success.
    goto cmd_error;
  }

  // All is well if we get here.
  goto ending;

cmd_error:
  diagPrintLn(F("doHTTPGET failed!"));

ending:
  off();
  return retval;
}

bool GPRSbeeClass::setBearerParms(const char *apn, const char *user, const char *pwd)
{
  char cmd[64];
  bool retval = false;
  int retry;

  // SAPBR=3 Set bearer parameters
  if (!sendCommandWaitForOK_P(PSTR("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""))) {
    goto ending;
  }

  // SAPBR=3 Set bearer parameters
  strcpy_P(cmd, PSTR("AT+SAPBR=3,1,\"APN\",\""));
  strcat(cmd, apn);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }
  if (user && user[0]) {
    strcpy_P(cmd, PSTR("AT+SAPBR=3,1,\"USER\",\""));
    strcat(cmd, user);
    strcat(cmd, "\"");
    if (!sendCommandWaitForOK(cmd)) {
      goto ending;
    }
  }
  if (pwd && pwd[0]) {
    strcpy_P(cmd, PSTR("AT+SAPBR=3,1,\"PWD\",\""));
    strcat(cmd, pwd);
    strcat(cmd, "\"");
    if (!sendCommandWaitForOK(cmd)) {
      goto ending;
    }
  }

  // SAPBR=1 Open bearer
  // This command can fail if signal quality is low, or if we're too fast
  for (retry = 0; retry < 5; retry++) {
    if (sendCommandWaitForOK_P(PSTR("AT+SAPBR=1,1"),10000)) {
      break;
    }
  }
  if (retry >= 5) {
    goto ending;
  }

  // SAPBR=2 Query bearer
  // Expect +SAPBR: <cid>,<Status>,<IP_Addr>
  if (!sendCommandWaitForOK_P(PSTR("AT+SAPBR=2,1"))) {
    goto ending;
  }

  retval = true;

ending:
  return retval;
}

bool GPRSbeeClass::getIMEI(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 2000;
  return getStrValue("AT+GSN", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getGCAP(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 2000;
  return getStrValue("AT+GCAP", "+GCAP:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCIMI(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 2000;
  return getStrValue("AT+CIMI", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCLIP(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+CLIP?", "+CLIP:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCLIR(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+CLIR?", "+CLIR:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCOLP(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+COLP?", "+COLP:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCOPS(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+COPS?", "+COPS:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCCLK(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+CCLK?", "+CCLK:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCSPN(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+CSPN?", "+CSPN:", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCGID(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue("AT+CGID", "+GID:", buffer, buflen, ts_max);
}

void GPRSbeeClass::enableLTS()
{
  if (!sendCommandWaitForOK_P(PSTR("AT+CLTS=1"), 6000)) {
  }
}

void GPRSbeeClass::disableLTS()
{
  if (!sendCommandWaitForOK_P(PSTR("AT+CLTS=0"), 6000)) {
  }
}
