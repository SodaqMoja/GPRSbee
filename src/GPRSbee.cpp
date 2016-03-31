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
#include <avr/pgmspace.h>
#ifdef ARDUINO_ARCH_AVR
#include <avr/wdt.h>
#else
#define wdt_reset()
#endif
#include <stdlib.h>

#include "GPRSbee.h"

#if ENABLE_GPRSBEE_DIAG
#define diagPrint(...) { if (_diagStream) _diagStream->print(__VA_ARGS__); }
#define diagPrintLn(...) { if (_diagStream) _diagStream->println(__VA_ARGS__); }
#else
#define diagPrint(...)
#define diagPrintLn(...)
#endif


// A specialized class to switch on/off the GPRSbee module
// The VCC3.3 pin is switched by the Autonomo BEE_VCC pin
// The DTR pin is the actual ON/OFF pin, it is A13 on Autonomo, D20 on Tatu
class GPRSbeeOnOff : public Sodaq_OnOffBee
{
public:
    GPRSbeeOnOff();
    void init(int vcc33Pin, int onoffPin, int statusPin);
    void on();
    void off();
    bool isOn();
private:
    int8_t _vcc33Pin;
    int8_t _onoffPin;
    int8_t _statusPin;
};

static GPRSbeeOnOff gprsbee_onoff;

GPRSbeeClass gprsbee;

/*
 * A wrapper for delay that also resets the WDT while waiting
 */
static inline void mydelay(unsigned long nrMillis)
{
  const unsigned long d = 10;
  while (nrMillis > d) {
    wdt_reset();
    delay(d);
    nrMillis -= d;
  }
  delay(nrMillis);
}

void GPRSbeeClass::initAutonomoSIM800(Stream &stream, int vcc33Pin, int onoffPin, int statusPin,
    int bufferSize)
{
  initProlog(stream, bufferSize);

  gprsbee_onoff.init(vcc33Pin, onoffPin, statusPin);
  _onoff = &gprsbee_onoff;
}

void GPRSbeeClass::initProlog(Stream &stream, size_t bufferSize)
{
  _inputBufferSize = bufferSize;
  initBuffer();

  _modemStream = &stream;
  _diagStream = 0;

  _ftpMaxLength = 0;
  _transMode = false;

  _echoOff = false;
  _skipCGATT = false;
  _changedSkipCGATT = false;

  _productId = prodid_unknown;

  _timeToOpenTCP = 0;
  _timeToCloseTCP = 0;
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
        // We didn't get an OK
        // Should we retry?
        return;
    }
    // Also disable URCs
    disableCIURC();
    _echoOff = true;
  }
}

void GPRSbeeClass::flushInput()
{
  int c;
  while ((c = _modemStream->read()) >= 0) {
    diagPrint((char)c);
  }
}

/*
 * \brief Read a line of input from SIM900
 */
int GPRSbeeClass::readLine(uint32_t ts_max)
{
  if (_inputBuffer == NULL) {
    return -1;
  }

  uint32_t ts_waitLF = 0;
  bool seenCR = false;
  int c;
  size_t bufcnt;

  //diagPrintLn(F("readLine"));
  bufcnt = 0;
  while (!isTimedOut(ts_max)) {
    wdt_reset();
    if (seenCR) {
      c = _modemStream->peek();
      // ts_waitLF is guaranteed to be non-zero
      if ((c == -1 && isTimedOut(ts_waitLF)) || (c != -1 && c != '\n')) {
        //diagPrint(F("readLine:  peek '")); diagPrint(c); diagPrintLn('\'');
        // Line ended with just <CR>. That's OK too.
        goto ok;
      }
      // Only \n should fall through
    }

    c = _modemStream->read();
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
      if (bufcnt < (_inputBufferSize - 1)) {    // Leave room for the terminating NUL
        _inputBuffer[bufcnt++] = c;
      }
    }
  }

  diagPrintLn(F("readLine timed out"));
  return -1;            // This indicates: timed out

ok:
  _inputBuffer[bufcnt] = 0;     // Terminate with NUL byte
  //diagPrint(F(" ")); diagPrintLn(_inputBuffer);
  return bufcnt;

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
    int c = _modemStream->read();
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
    if (strcmp_P(_inputBuffer, PSTR("OK")) == 0) {
      return true;
    }
    else if (strcmp_P(_inputBuffer, PSTR("ERROR")) == 0) {
      return false;
    }
    // Other input is skipped.
  }
  return false;
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
    if (strncmp(_inputBuffer, msg, strlen(msg)) == 0) {
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
    if (strncmp_P(_inputBuffer, msg, strlen_P(msg)) == 0) {
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
    //diagPrint(F(" checking \"")); diagPrint(_inputBuffer); diagPrintLn("\"");
    for (size_t i = 0; i < nrMsgs; ++i) {
      //diagPrint(F("  checking \"")); diagPrint(msgs[i]); diagPrintLn("\"");
      if (strcmp_P(_inputBuffer, msgs[i]) == 0) {
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

    int c = _modemStream->read();
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

/*
 * \brief Prepare for a new command
 */
void GPRSbeeClass::sendCommandProlog()
{
  flushInput();
  mydelay(50);                  // Without this we get lots of "readLine timed out". Unclear why
  diagPrint(F(">> "));
}

/*
 * \brief Add a part of the command (don't yet send the final CR)
 */
void GPRSbeeClass::sendCommandAdd(char c)
{
  diagPrint(c);
  _modemStream->print(c);
}
void GPRSbeeClass::sendCommandAdd(int i)
{
  diagPrint(i);
  _modemStream->print(i);
}
void GPRSbeeClass::sendCommandAdd(const char *cmd)
{
  diagPrint(cmd);
  _modemStream->print(cmd);
}
void GPRSbeeClass::sendCommandAdd(const String & cmd)
{
  diagPrint(cmd);
  _modemStream->print(cmd);
}
void GPRSbeeClass::sendCommandAdd_P(const char *cmd)
{
  diagPrint(reinterpret_cast<const __FlashStringHelper *>(cmd));
  _modemStream->print(reinterpret_cast<const __FlashStringHelper *>(cmd));
}

/*
 * \brief Send the final CR of the command
 */
void GPRSbeeClass::sendCommandEpilog()
{
  diagPrintLn();
  _modemStream->print('\r');
}

void GPRSbeeClass::sendCommand(const char *cmd)
{
  sendCommandProlog();
  sendCommandAdd(cmd);
  sendCommandEpilog();
}
void GPRSbeeClass::sendCommand_P(const char *cmd)
{
  sendCommandProlog();
  sendCommandAdd_P(cmd);
  sendCommandEpilog();
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
bool GPRSbeeClass::sendCommandWaitForOK(const String & cmd, uint16_t timeout)
{
  sendCommand(cmd.c_str());
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
    const char *ptr = _inputBuffer + strlen(reply);
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

bool GPRSbeeClass::getIntValue_P(const char *cmd, const char *reply, int * value, uint32_t ts_max)
{
  sendCommand_P(cmd);

  // First we expect the reply
  if (waitForMessage_P(reply, ts_max)) {
    const char *ptr = _inputBuffer + strlen_P(reply);
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
 * \brief Get SIM900 string value with the result of an AT command
 *
 *\param cmd    the AT command
 *\param reply  the prefix of the expected reply (this is stripped from the result
 *\param str    a pointer to where the result must be copied
 *\param size   the length of the result buffer
 *\param ts_max the maximum ts to wait for the reply
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
    const char *ptr = _inputBuffer + strlen(reply);
    // Strip leading white space
    while (*ptr != '\0' && *ptr == ' ') {
      ++ptr;
    }
    strncpy(str, ptr, size - 1);
    str[size - 1] = '\0';               // Terminate, just to be sure
    // Wait for "OK"
    return waitForOK();
  }
  return false;
}

bool GPRSbeeClass::getStrValue_P(const char *cmd, const char *reply, char * str, size_t size, uint32_t ts_max)
{
  sendCommand_P(cmd);

  if (waitForMessage_P(reply, ts_max)) {
    const char *ptr = _inputBuffer + strlen_P(reply);
    // Strip leading white space
    while (*ptr != '\0' && *ptr == ' ') {
      ++ptr;
    }
    strncpy(str, ptr, size - 1);
    str[size - 1] = '\0';               // Terminate, just to be sure
    // Wait for "OK"
    return waitForOK();
  }
  return false;
}

/*
 * \brief Get SIM900 string value with the result of an AT command
 *
 *\param cmd    the AT command
 *\param str    a pointer to where the result must be copied
 *\param size   the length of the result buffer
 *\param ts_max the maximum ts to wait for the reply
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
    strncpy(str, _inputBuffer, size - 1);
    str[size - 1] = '\0';               // Terminate, just to be sure
    break;
  }
  if (len < 0) {
      // There was a timeout
      return false;
  }
  // Wait for "OK"
  return waitForOK();
}

// Sets the apn, apn username and apn password to the modem.
bool GPRSbeeClass::sendAPN(const char* apn, const char* username, const char* password)
{
    return false;
}

// Turns on and initializes the modem, then connects to the network and activates the data connection.
bool GPRSbeeClass::connect(const char* apn, const char* username, const char* password)
{
    // TODO
    return false;
}

// Returns true if the modem is connected to the network and has an activated data connection.
bool GPRSbeeClass::isConnected()
{
    // TODO
    return false;
}

// Disconnects the modem from the network.
bool GPRSbeeClass::disconnect()
{
    // TODO
    return false;
}

/*!
 * \brief Utility function to do waitForSignalQuality and waitForCREG
 */
bool GPRSbeeClass::networkOn()
{
  bool status;
  status = on();
  if (status) {
    // Suppress echoing
    switchEchoOff();

    status = waitForSignalQuality();
    if (status) {
      status = waitForCREG();
    }
  }
  return status;
}

// Gets the Received Signal Strength Indication in dBm and Bit Error Rate.
// Returns true if successful.
bool GPRSbeeClass::getRSSIAndBER(int8_t* rssi, uint8_t* ber)
{
    static char berValues[] = { 49, 43, 37, 25, 19, 13, 7, 0 }; // 3GPP TS 45.008 [20] subclause 8.2.4
    int rssiRaw = 0;
    int berRaw = 0;
    // TODO get BER value
    if (getIntValue("AT+CSQ", "+CSQ:", &rssiRaw, millis() + 12000 )) {
        *rssi = ((rssiRaw == 99) ? 0 : -113 + 2 * rssiRaw);
        *ber = ((berRaw == 99 || static_cast<size_t>(berRaw) >= sizeof(berValues)) ? 0 : berValues[berRaw]);

        return true;
    }

    return false;
}

bool GPRSbeeClass::waitForSignalQuality()
{
    /*
     * The timeout is just a wild guess. If the mobile connection
     * is really bad, or even absent, then it is a waste of time
     * (and battery) to even try.
     */
    uint32_t start = millis();
    uint32_t ts_max = start + 30000;
    int8_t rssi;
    uint8_t ber;

    while (!isTimedOut(ts_max)) {
        if (getRSSIAndBER(&rssi, &ber)) {
            if (rssi != 0 && rssi >= _minSignalQuality) {
                _lastRSSI = rssi;
                _CSQtime = (int32_t) (millis() - start) / 1000;
                return true;
            }
        }
        /*sodaq_wdt_safe_*/ delay(500);
    }
    _lastRSSI = 0;
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
      const char *ptr = strchr(_inputBuffer, ',');
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

/*!
 * \brief Do a few common things to start a connection
 *
 * Do a few things that are common for setting up
 * a connection for TCP, FTP and HTTP.
 */
bool GPRSbeeClass::connectProlog()
{
  // TODO Use networkOn instead of switchEchoOff, waitForSignalQuality, waitForCREG

  // Suppress echoing
  switchEchoOff();

  // Wait for signal quality
  if (!waitForSignalQuality()) {
    return false;
  }

  // Wait for CREG
  if (!waitForCREG()) {
    return false;
  }

  if (!_changedSkipCGATT && _productId == prodid_unknown) {
    // Try to figure out what kind it is. SIM900? SIM800? etc.
    setProductId();
    if (_productId == prodid_SIM800) {
      _skipCGATT = true;
    }
  }

  // Attach to GPRS service
  // We need a longer timeout than the normal waitForOK
  if (!_skipCGATT && !sendCommandWaitForOK_P(PSTR("AT+CGATT=1"), 30000)) {
    return false;
  }

  return true;
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

  if (!connectProlog()) {
    goto cmd_error;
  }

  // AT+CSTT=<apn>,<username>,<password>
  strcpy_P(cmdbuf, PSTR("AT+CSTT=\""));
  strcat(cmdbuf, apn);
  strcat(cmdbuf, "\",\"");
  if (apnuser) {
    strcat(cmdbuf, apnuser);
  }
  strcat(cmdbuf, "\",\"");
  if (apnpwd) {
    strcat(cmdbuf, apnpwd);
  }
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
  _timeToOpenTCP = millis() - _startOn;
  goto ending;

cmd_error:
  diagPrintLn(F("openTCP failed!"));
  off();

ending:
  return retval;
}

void GPRSbeeClass::closeTCP(bool switchOff)
{
  uint32_t ts_max;
  // AT+CIPSHUT
  // Maybe we should do AT+CIPCLOSE=1
  if (_transMode) {
    mydelay(1000);
    _modemStream->print(F("+++"));
    mydelay(500);
    // TODO Will the SIM900 answer with "OK"?
  }
  sendCommand_P(PSTR("AT+CIPSHUT"));
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage_P(PSTR("SHUT OK"), ts_max)) {
    diagPrintLn(F("closeTCP failed!"));
  }

  if (switchOff) {
    off();
  }
  _timeToCloseTCP = millis() - _startOn;
}

bool GPRSbeeClass::isTCPConnected()
{
  uint32_t ts_max;
  bool retval = false;
  const char *ptr;

  if (!isOn()) {
    goto end;
  }

  if (_transMode) {
    // We need to send +++
    mydelay(1000);
    _modemStream->print(F("+++"));
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
  ptr = _inputBuffer + 6;
  ptr = skipWhiteSpace(ptr);
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

/*!
 * \brief Send some data over the TCP connection
 */
bool GPRSbeeClass::sendDataTCP(const uint8_t *data, size_t data_len)
{
  uint32_t ts_max;
  bool retval = false;

  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+CIPSEND="));
  sendCommandAdd((int)data_len);
  sendCommandEpilog();
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForPrompt("> ", ts_max)) {
    goto error;
  }
  mydelay(50);          // TODO Why do we need this?
  // Send the data
  for (size_t i = 0; i < data_len; ++i) {
    _modemStream->print((char)*data++);
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

/*!
 * \brief Receive a number of bytes from the TCP connection
 *
 * If there are not enough bytes then this function will time
 * out, and it will return false.
 */
bool GPRSbeeClass::receiveDataTCP(uint8_t *data, size_t data_len, uint16_t timeout)
{
  uint32_t ts_max;
  bool retval = false;

  //diagPrintLn(F("receiveDataTCP"));
  ts_max = millis() + timeout;
  while (data_len > 0 && !isTimedOut(ts_max)) {
    if (_modemStream->available() > 0) {
      uint8_t b;
      b = _modemStream->read();
      *data++ = b;
      --data_len;
    }
  }
  if (data_len == 0) {
    retval = true;
  }

  return retval;
}

/*!
 * \brief Receive a line of ASCII via the TCP connection
 */
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
  *buffer = _inputBuffer;
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

  if (!connectProlog()) {
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
  const char * ptr;
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
      if (!waitForMessage_P(PSTR("+FTPPUT:"), ts_max)) {
        // Try again.
        isAlive();
        continue;
      }
      // Skip 8 for "+FTPPUT:"
      ptr = _inputBuffer + 8;
      ptr = skipWhiteSpace(ptr);
      if (strncmp_P(ptr, PSTR("1,"), 2) != 0) {
        // We did NOT get "+FTPPUT:1,1,", it might be an error.
        goto ending;
      }
      ptr += 2;

      if (strncmp_P(ptr, PSTR("1,"), 2) != 0) {
        // We did NOT get "+FTPPUT:1,1,", it might be an error.
        goto ending;
      }
      ptr += 2;

      _ftpMaxLength = strtoul(ptr, NULL, 0);

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
  if (!waitForMessage_P(PSTR("+FTPPUT:"), ts_max)) {
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
  if (!waitForMessage_P(PSTR("+FTPPUT:"), ts_max)) {
    // How bad is it if we ignore this
    return false;
  }
  mydelay(100);           // TODO Find out if we can drop this

  // Send data ...
  for (size_t i = 0; i < size; ++i) {
    _modemStream->print((char)*ptr++);
  }
  //_modemStream->print('\r');          // dummy <CR>, not sure if this is needed

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
  if (!waitForMessage_P(PSTR("+FTPPUT:"), ts_max)) {
    // How bad is it if we ignore this?
    // It informs us about the _ftpMaxLength
  }

  return true;
}

bool GPRSbeeClass::sendFTPdata_low(uint8_t (*read)(), size_t size)
{
  char cmd[20];         // Should be enough for "AT+FTPPUT=2,<num>"
  const char * ptr;
  uint32_t ts_max;

  // Send some data
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUT=2,%d", size);
  strcpy_P(cmd, PSTR("AT+FTPPUT=2,"));
  itoa(size, cmd + strlen(cmd), 10);
  sendCommand(cmd);

  ts_max = millis() + 10000;
  // +FTPPUT:2,22
  if (!waitForMessage_P(PSTR("+FTPPUT:"), ts_max)) {
    ptr = _inputBuffer + 8;
    if (strncmp_P(ptr, PSTR("2,"), 2) != 0) {
      // We did NOT get "+FTPPUT:2,", it might be an error.
      return false;
    }
    ptr += 2;
    // TODO Check for the number
    // How bad is it if we ignore this
    return false;
  }
  mydelay(100);           // TODO Find out if we can drop this

  // Send data ...
  for (size_t i = 0; i < size; ++i) {
    _modemStream->print((char)(*read)());
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
  if (!waitForMessage_P(PSTR("+FTPPUT:"), ts_max)) {
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
  _modemStream->print(text); //the message itself
  _modemStream->print((char)26); //the ASCII code of ctrl+z is 26, this is needed to end the send modus and send the message.
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

/*!
 * \brief The middle part of the whole HTTP POST
 *
 * This function does:
 *  - HTTPPARA with the URL
 *  - HTTPDATA
 *  - HTTPACTION(1)
 */
bool GPRSbeeClass::doHTTPPOSTmiddle(const char *url, const char *buffer, size_t len)
{
  uint32_t ts_max;
  bool retval = false;
  char num_bytes[16];

  // set http param URL value
  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+HTTPPARA=\"URL\",\""));
  sendCommandAdd(url);
  sendCommandAdd('"');
  sendCommandEpilog();
  if (!waitForOK()) {
    goto ending;
  }

  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+HTTPDATA="));
  itoa(len, num_bytes, 10);
  sendCommandAdd(num_bytes);
  sendCommandAdd_P(PSTR(",10000"));
  sendCommandEpilog();
  ts_max = millis() + 4000;
  if (!waitForMessage_P(PSTR("DOWNLOAD"), ts_max)) {
    goto ending;
  }

  // Send data ...
  for (size_t i = 0; i < len; ++i) {
    _modemStream->print(*buffer++);
  }

  if (!waitForOK()) {
    goto ending;
  }

  if (!doHTTPACTION(1)) {
    goto ending;
  }

  // All is well if we get here.
  retval = true;

ending:
  return retval;
}

/*!
 * \brief The middle part of the whole HTTP POST, with a READ
 *
 * This function does:
 *  - doHTTPPOSTmiddle() ...
 *  - HTTPREAD
 */
bool GPRSbeeClass::doHTTPPOSTmiddleWithReply(const char *url, const char *postdata, size_t pdlen, char *buffer, size_t len)
{
  bool retval = false;;

  if (!doHTTPPOSTmiddle(url, postdata, pdlen)) {
    goto ending;
  }

  // Read all data
  if (!doHTTPREAD(buffer, len)) {
    goto ending;
  }

  // All is well if we get here.
  retval = true;

ending:
    return retval;
}

/*!
 * \brief The middle part of the whole HTTP GET
 *
 * This function does:
 *  - HTTPPARA with the URL
 *  - HTTPACTION(0)
 *  - HTTPREAD
 */
bool GPRSbeeClass::doHTTPGETmiddle(const char *url, char *buffer, size_t len)
{
  bool retval = false;

  // set http param URL value
  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+HTTPPARA=\"URL\",\""));
  sendCommandAdd(url);
  sendCommandAdd('"');
  sendCommandEpilog();
  if (!waitForOK()) {
    goto ending;
  }

  if (!doHTTPACTION(0)) {
    goto ending;
  }

  // Read all data
  if (!doHTTPREAD(buffer, len)) {
    goto ending;
  }

  // All is well if we get here.
  retval = true;

ending:
  return retval;
}

bool GPRSbeeClass::doHTTPprolog(const char *apn)
{
  return doHTTPprolog(apn, 0, 0);
}

bool GPRSbeeClass::doHTTPprolog(const char *apn, const char *apnuser, const char *apnpwd)
{
  bool retval = false;

  if (!connectProlog()) {
    goto ending;
  }

  if (!setBearerParms(apn, apnuser, apnpwd)) {
    goto ending;
  }

  // initialize http service
  if (!sendCommandWaitForOK_P(PSTR("AT+HTTPINIT"))) {
    goto ending;
  }

  // set http param CID value
  // FIXME Do we really need this?
  if (!sendCommandWaitForOK_P(PSTR("AT+HTTPPARA=\"CID\",1"))) {
    goto ending;
  }

  retval = true;

ending:
  return retval;
}

void GPRSbeeClass::doHTTPepilog()
{
  if (!sendCommandWaitForOK_P(PSTR("AT+HTTPTERM"))) {
    // This is an error, but we can still return success.
  }
}

/*
 * \brief Read the data from a GET or POST
 */
bool GPRSbeeClass::doHTTPREAD(char *buffer, size_t len)
{
  uint32_t ts_max;
  size_t getLength = 0;
  int i;
  bool retval = false;

  // Expect
  //   +HTTPREAD:<date_len>
  //   <data>
  //   OK
  sendCommand_P(PSTR("AT+HTTPREAD"));
  ts_max = millis() + 8000;
  if (waitForMessage_P(PSTR("+HTTPREAD:"), ts_max)) {
    const char *ptr = _inputBuffer + 10;
    char *bufend;
    getLength = strtoul(ptr, &bufend, 0);
    if (bufend == ptr) {
      // Invalid number
      goto ending;
    }
  } else {
    // Hmm. Why didn't we get this?
    goto ending;
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
  }

  // All is well if we get here.

ending:
  return retval;
}

bool GPRSbeeClass::doHTTPACTION(char num)
{
  uint32_t ts_max;
  bool retval = false;

  // set http action type 0 = GET, 1 = POST, 2 = HEAD
  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+HTTPACTION="));
  sendCommandAdd((int)num);
  sendCommandEpilog();
  if (!waitForOK()) {
    goto ending;
  }
  // Now we're expecting something like this: +HTTPACTION: <Method>,<StatusCode>,<DataLen>
  // <Method> 0
  // <StatusCode> 200
  // <DataLen> ??
  ts_max = millis() + 20000;
  if (waitForMessage_P(PSTR("+HTTPACTION:"), ts_max)) {
    // SIM900 responds with: "+HTTPACTION:1,200,11"
    // SIM800 responds with: "+HTTPACTION: 1,200,11"
    // The 12 is the length of "+HTTPACTION:"
    // We then have to skip the digit and the comma
    const char *ptr = _inputBuffer + 12;
    ptr = skipWhiteSpace(ptr);
    ++ptr;              // The digit
    ++ptr;              // The comma
    char *bufend;
    uint8_t replycode = strtoul(ptr, &bufend, 0);
    if (bufend == ptr) {
      // Invalid number
      goto ending;
    }
    // TODO Which result codes are allowed to pass?
    if (replycode == 200) {
      retval = true;
    } else {
      // Everything else is considered an error
    }
  }

  // All is well if we get here.

ending:
  return retval;
}

bool GPRSbeeClass::doHTTPPOST(const char *apn, const char *url, const char *postdata, size_t pdlen)
{
  return doHTTPPOST(apn, 0, 0, url, postdata, pdlen);
}

bool GPRSbeeClass::doHTTPPOST(const char *apn, const char *apnuser, const char *apnpwd,
    const char *url, const char *postdata, size_t pdlen)
{
  bool retval = false;

  if (!on()) {
    goto ending;
  }

  if (!doHTTPprolog(apn, apnuser, apnpwd)) {
    goto cmd_error;
  }

  if (!doHTTPPOSTmiddle(url, postdata, pdlen)) {
    goto cmd_error;
  }

  retval = true;
  doHTTPepilog();
  goto ending;

cmd_error:
  diagPrintLn(F("doHTTPGET failed!"));

ending:
  off();
  return retval;
}


bool GPRSbeeClass::doHTTPPOSTWithReply(const char *apn,
    const char *url, const char *postdata, size_t pdlen, char *buffer, size_t len)
{
  return doHTTPPOSTWithReply(apn, 0, 0, url, postdata, pdlen, buffer, len);
}

bool GPRSbeeClass::doHTTPPOSTWithReply(const char *apn, const char *apnuser, const char *apnpwd,
    const char *url, const char *postdata, size_t pdlen, char *buffer, size_t len)
{
  bool retval = false;

  if (!on()) {
    goto ending;
  }

  if (!doHTTPprolog(apn, apnuser, apnpwd)) {
    goto cmd_error;
  }

  if (!doHTTPPOSTmiddleWithReply(url, postdata, pdlen, buffer, len)) {
    goto cmd_error;
  }

  retval = true;
  doHTTPepilog();
  goto ending;

cmd_error:
  diagPrintLn(F("doHTTPGET failed!"));

ending:
  off();
  return retval;
}

bool GPRSbeeClass::doHTTPGET(const char *apn, const char *url, char *buffer, size_t len)
{
  return doHTTPGET(apn, 0, 0, url, buffer, len);
}

bool GPRSbeeClass::doHTTPGET(const char *apn, const String & url, char *buffer, size_t len)
{
  return doHTTPGET(apn, 0, 0, url.c_str(), buffer, len);
}

bool GPRSbeeClass::doHTTPGET(const char *apn, const char *apnuser, const char *apnpwd,
    const char *url, char *buffer, size_t len)
{
  bool retval = false;

  if (!on()) {
    goto ending;
  }

  if (!doHTTPprolog(apn, apnuser, apnpwd)) {
    goto cmd_error;
  }

  if (!doHTTPGETmiddle(url, buffer, len)) {
    goto cmd_error;
  }

  retval = true;
  doHTTPepilog();
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
  return getStrValue_P(PSTR("AT+GCAP"), PSTR("+GCAP:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCIMI(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 2000;
  return getStrValue("AT+CIMI", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCCID(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 2000;
  return getStrValue("AT+CCID", buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCLIP(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+CLIP?"), PSTR("+CLIP:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCLIR(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+CLIR?"), PSTR("+CLIR:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCOLP(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+COLP?"), PSTR("+COLP:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCOPS(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+COPS?"), PSTR("+COPS:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::setCCLK(const SIMDateTime & dt)
{
  String str;
  str.reserve(30);
  dt.addToString(str);
  switchEchoOff();
  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+CCLK=\""));
  sendCommandAdd(str);
  sendCommandAdd('"');
  sendCommandEpilog();
  return waitForOK();
}

bool GPRSbeeClass::getCCLK(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+CCLK?"), PSTR("+CCLK:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCSPN(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+CSPN?"), PSTR("+CSPN:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::getCGID(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+CGID"), PSTR("+GID:"), buffer, buflen, ts_max);
}

bool GPRSbeeClass::setCIURC(uint8_t value)
{
  switchEchoOff();
  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+CIURC="));
  sendCommandAdd((int)value);
  sendCommandEpilog();
  return waitForOK();
}

bool GPRSbeeClass::getCIURC(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  return getStrValue_P(PSTR("AT+CIURC?"), PSTR("+CIURC:"), buffer, buflen, ts_max);
}

/*
 * \brief Set the AT+CFUN value (Set Phone Functionality)
 *
 * Allowed values are
 * - 0 Minimum functionality
 * - 1 Full functionality (Default)
 * - 4 Disable phone both transmit and receive RF circuits
 */
bool GPRSbeeClass::setCFUN(uint8_t value)
{
  switchEchoOff();
  sendCommandProlog();
  sendCommandAdd_P(PSTR("AT+CFUN="));
  sendCommandAdd((int)value);
  sendCommandEpilog();
  return waitForOK();
}

bool GPRSbeeClass::getCFUN(uint8_t * value)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 4000;
  int tmpValue;
  bool status;
  status = getIntValue_P(PSTR("AT+CFUN?"), PSTR("+CFUN:"), &tmpValue, ts_max);
  if (status) {
    *value = (uint8_t)tmpValue;
  }
  return status;
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

void GPRSbeeClass::enableCIURC()
{
  if (!sendCommandWaitForOK_P(PSTR("AT+CIURC=1"), 6000)) {
  }
}

void GPRSbeeClass::disableCIURC()
{
  if (!sendCommandWaitForOK_P(PSTR("AT+CIURC=0"), 6000)) {
  }
}

/*!
 * \brief Get Product Identification Information
 *
 * Send the ATI command and get the result.
 * SIM900 is expected to return something like:
 *    SIM900 R11.0
 * SIM800 is expected to return something like:
 *    SIM800 R11.08
 */
bool GPRSbeeClass::getPII(char *buffer, size_t buflen)
{
  switchEchoOff();
  uint32_t ts_max = millis() + 2000;
  return getStrValue("ATI", buffer, buflen, ts_max);
}

void GPRSbeeClass::setProductId()
{
  char buffer[64];
  if (getPII(buffer, sizeof(buffer))) {
    if (strncmp_P(buffer, PSTR("SIM900"), 6) == 0) {
      _productId = prodid_SIM900;
    }
    else if (strncmp_P(buffer, PSTR("SIM800"), 6) == 0) {
      _productId = prodid_SIM800;
    }
  }
}

const char * GPRSbeeClass::skipWhiteSpace(const char * txt)
{
  while (*txt != '\0' && *txt == ' ') {
    ++txt;
  }
  return txt;
}

uint32_t GPRSbeeClass::getUnixEpoch() const
{
  bool status;
  char buffer[64];

  status = false;
  for (uint8_t ix = 0; !status && ix < 10; ++ix) {
    status = gprsbee.on();
  }

  status = false;
  for (uint8_t ix = 0; !status && ix < 10; ++ix) {
    status = gprsbee.getCCLK(buffer, sizeof(buffer));
  }

  const char * ptr = buffer;
  if (*ptr == '"') {
    ++ptr;
  }
  SIMDateTime dt = SIMDateTime(ptr);

  return dt.getUnixEpoch();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

SIMDateTime::SIMDateTime(uint8_t y, uint8_t m, uint8_t d, uint8_t hh, uint8_t mm, uint8_t ss, int8_t tz)
{
  _yOff = y;
  _m = m;
  _d = d;
  _hh = hh;
  _mm = mm;
  _ss = ss;
  _tz = tz;
}

/*
 * \brief Construct from a timestamp (seconds since Y2K Epoch)
 */
SIMDateTime::SIMDateTime(uint32_t ts)
{
  // whole and fractional parts of 1 day
  uint16_t days = ts / 86400UL;
  int32_t fract = ts % 86400UL;
  //Serial.print(F("days ")); Serial.println(days);

  // Extract hour, minute, and second from the fractional day
  ldiv_t lresult = ldiv(fract, 60L);
  _ss = lresult.rem;
  div_t result = div(lresult.quot, 60);
  _mm = result.rem;
  _hh = result.quot;

  //
  uint16_t n = days + SATURDAY;
  n %= 7;
  //Serial.print(F("wday ")); Serial.println(n);
  // _wday = n;

  // map into a 100 year cycle
  lresult = ldiv((long) days, 36525L);
  uint16_t years = 100 * lresult.quot;
  //Serial.print(F("years ")); Serial.println(years);

  // map into a 4 year cycle
  lresult = ldiv(lresult.rem, 1461L);
  years += 4 * lresult.quot;
  days = lresult.rem;
  if (years > 100) {
    ++days;
  }

  // 'years' is now at the first year of a 4 year leap cycle, which will always be a leap year,
  // unless it is 100. 'days' is now an index into that cycle.
  uint8_t leapyear = 1;
  if (years == 100) {
    leapyear = 0;
  }

  // compute length, in days, of first year of this cycle
  n = 364 + leapyear;

  /*
   * if the number of days remaining is greater than the length of the
   * first year, we make one more division.
   */
  if (days > n) {
      days -= leapyear;
      leapyear = 0;
      result = div(days, 365);
      years += result.quot;
      days = result.rem;
  }
  //Serial.print(F("years ")); Serial.println(years);
  _yOff = years;
  //Serial.print(F("days ")); Serial.println(days);
  // _ydays = days;

  /*
   * Given the year, day of year, and leap year indicator, we can break down the
   * month and day of month. If the day of year is less than 59 (or 60 if a leap year), then
   * we handle the Jan/Feb month pair as an exception.
   */
  n = 59 + leapyear;
  if (days < n) {
      /* special case: Jan/Feb month pair */
      result = div(days, 31);
      _m = result.quot;
      _d = result.rem;
  } else {
      /*
       * The remaining 10 months form a regular pattern of 31 day months alternating with 30 day
       * months, with a 'phase change' between July and August (153 days after March 1).
       * We proceed by mapping our position into either March-July or August-December.
       */
      days -= n;
      result = div(days, 153);
      _m = 2 + result.quot * 5;

      /* map into a 61 day pair of months */
      result = div(result.rem, 61);
      _m += result.quot * 2;

      /* map into a month */
      result = div(result.rem, 31);
      _m += result.quot;
      _d = result.rem;
  }

  _tz = 0;
}

/*
 * \brief Construct using a text string as received from AT+CCLK
 *
 * format is "yy/MM/dd,hh:mm:ss±zz"
 *
 * No serious attempt is made to validate the string. Whatever comes
 * in is used as is. Each number is assumed to have two digits.
 *
 * Also, you year number is assumed to be the offset of 2000
 *
 * Example input string: 04/01/02,00:47:32+04
 */
SIMDateTime::SIMDateTime(const char * cclk)
{
  _yOff = conv2d(cclk);
  cclk += 3;
  _m = conv2d(cclk) - 1;                // Month is 0 based
  cclk += 3;
  _d = conv2d(cclk) - 1;                // Day is 0 based
  cclk += 3;
  _hh = conv2d(cclk);
  cclk += 3;
  _mm = conv2d(cclk);
  cclk += 3;
  _ss = conv2d(cclk);
  cclk += 2;
  uint8_t isNeg = *cclk == '-';
  ++cclk;
  _tz = conv2d(cclk);
  if (isNeg) {
    _tz = -_tz;
  }
}

static const uint8_t daysInMonth [] PROGMEM = { 31,28,31,30,31,30,31,31,30,31,30,31 };

/*
 * \brief Compute the Y2K Epoch from the date and time
 */
uint32_t SIMDateTime::getY2KEpoch() const
{
  uint32_t ts;
  uint16_t days = _d + (365 * _yOff) + ((_yOff + 3) / 4);
  // Add the days of the previous months in this year.
  for (uint8_t i = 0; i < _m; ++i) {
    days += pgm_read_byte(daysInMonth + i);
  }
  if ((_m > 2) && ((_yOff % 4) == 0)) {
    ++days;
  }

  ts = ((uint32_t)days * 24) + _hh;
  ts = (ts * 60) + _mm;
  ts = (ts * 60) + _ss;

  ts = ts - (_tz * 15 * 60);

  return ts;
}

/*
 * \brief Compute the UNIX Epoch from the date and time
 */
uint32_t SIMDateTime::getUnixEpoch() const
{
  return getY2KEpoch() + 946684800;
}

/*
 * \brief Convert a single digit to a number
 */
uint8_t SIMDateTime::conv1d(const char * txt)
{
  uint8_t       val = 0;
  if (*txt >= '0' && *txt <= '9') {
    val = *txt - '0';
  }
  return val;
}

/*
 * \brief Convert two digits to a number
 */
uint8_t SIMDateTime::conv2d(const char * txt)
{
  uint8_t val = conv1d(txt++) * 10;
  val += conv1d(txt);
  return val;
}

/*
 * Format an integer as %0*d
 *
 * Arduino formatting sucks.
 */
static void add0Nd(String &str, uint16_t val, size_t width)
{
  if (width >= 5 && val < 10000) {
    str += '0';
  }
  if (width >= 4 && val < 1000) {
    str += '0';
  }
  if (width >= 3 && val < 100) {
    str += '0';
  }
  if (width >= 2 && val < 10) {
    str += '0';
  }
  str += val;
}

/*
 * \brief Add to the String the text for the AT+CCLK= command
 *
 * The String is expected to already have enough reserved space
 * so that an out-of-memory is not likely.
 * The format is "yy/MM/dd,hh:mm:ss±zz"
 * For the time being the timezone is set to 0 (UTC)
 */
void SIMDateTime::addToString(String & str) const
{
  add0Nd(str, _yOff, 2);
  str += '/';
  add0Nd(str, _m + 1, 2);
  str += '/';
  add0Nd(str, _d + 1, 2);
  str += ',';
  add0Nd(str, _hh, 2);
  str += ':';
  add0Nd(str, _mm, 2);
  str += ':';
  add0Nd(str, _ss, 2);
  str += "+00";
}

////////////////////////////////////////////////////////////////////////////////
////////////////////    MQTT               /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

bool GPRSbeeClass::openMQTT(const char * server, uint16_t port)
{
    if (!on()) {
        return false;
    }
    if (!networkOn()) {
        return false;
    }
    return openTCP(_apn, _apnUser, _apnPass, server, port);
}

bool GPRSbeeClass::closeMQTT(bool switchOff)
{
    closeTCP(switchOff);
    return true;        // Always succeed
}

bool GPRSbeeClass::sendMQTTPacket(uint8_t * pckt, size_t len)
{
    return sendDataTCP(pckt, len);
}

bool GPRSbeeClass::receiveMQTTPacket(uint8_t * pckt, size_t expected_len)
{
    return receiveDataTCP(pckt, expected_len);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////    GPRSbeeOnOff       /////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

GPRSbeeOnOff::GPRSbeeOnOff()
{
    _vcc33Pin = -1;
    _onoffPin = -1;
    _statusPin = -1;
}

// Initializes the instance
void GPRSbeeOnOff::init(int vcc33Pin, int onoffPin, int statusPin)
{
    if (vcc33Pin >= 0) {
      _vcc33Pin = vcc33Pin;
      // First write the output value, and only then set the output mode.
      digitalWrite(_vcc33Pin, LOW);
      pinMode(_vcc33Pin, OUTPUT);
    }

    if (onoffPin >= 0) {
      _onoffPin = onoffPin;
      // First write the output value, and only then set the output mode.
      digitalWrite(_onoffPin, LOW);
      pinMode(_onoffPin, OUTPUT);
    }

    if (statusPin >= 0) {
      _statusPin = statusPin;
      pinMode(_statusPin, INPUT);
    }
}

void GPRSbeeOnOff::on()
{
    // First VCC 3.3 HIGH
    if (_vcc33Pin >= 0) {
        digitalWrite(_vcc33Pin, HIGH);
    }

    // Wait a little
    // TODO Figure out if this is really needed
    delay(2);
    if (_onoffPin >= 0) {
        digitalWrite(_onoffPin, HIGH);
    }
}

void GPRSbeeOnOff::off()
{
    if (_vcc33Pin >= 0) {
        digitalWrite(_vcc33Pin, LOW);
    }

    // The GPRSbee is switched off immediately
    if (_onoffPin >= 0) {
        digitalWrite(_onoffPin, LOW);
    }

    // Should be instant
    // Let's wait a little, but not too long
    delay(50);
}

bool GPRSbeeOnOff::isOn()
{
    if (_statusPin >= 0) {
        bool status = digitalRead(_statusPin);
        return status;
    }

    // No status pin. Let's assume it is on.
    return true;
}
