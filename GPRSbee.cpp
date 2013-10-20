/*
 * GPRSbee.cpp
 *
 *  Created on: Oct 19, 2013
 *      Author: Kees Bakker
 */

#include <Arduino.h>
#include <Stream.h>

#include "GPRSbee.h"



GPRSbeeClass gprsbee;

void GPRSbeeClass::init(Stream &stream, int ctsPin, int powerPin)
{
  _myStream = &stream;
  _ctsPin = ctsPin;
  _powerPin = powerPin;
  _minSignalQuality = 10;
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
    if (waitForMessage("NORMAL POWER DOWN", ts_max)) {
      // OK. The SIM900 is switched off
    } else {
      // Should we care if it didn't?
    }
  }
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
  delay(1000);
#endif
  digitalWrite(_powerPin, HIGH);
  delay(2500);
  digitalWrite(_powerPin, LOW);
}

bool GPRSbeeClass::isAlive()
{
  // Send "AT" and wait for "OK"
  // Try it at least 3 times before deciding it failed
  for (int i = 0; i < 3; i++) {
    sendCommand("AT");
    if (waitForOK()) {
      return true;
    }
  }
  return false;
}

void GPRSbeeClass::flushInput()
{
  int c;
  while ((c = _myStream->read()) >= 0) {
    //DIAGPRINT((char)c);
  }
}

/*
 * \brief Read a line of input from SIM900
 */
int GPRSbeeClass::readLine(uint32_t ts_max)
{
  uint32_t ts_waitLF = 0;
  bool seenCR = false;

  _SIM900_bufcnt = 0;
  while (!isTimedOut(ts_max)) {
    int c = Serial1.read();
    if (c < 0) {
      if (seenCR && isTimedOut(ts_waitLF)) {
        // Line ended with just <CR>. That's OK too.
        goto ok;
      }
      delay(50);
      continue;
    }
    if (c != '\r') {
      //DIAGPRINT((char)c);
    }
    if (c == '\r') {
      seenCR = true;
      ts_waitLF = millis() + 500;       // Wait another .5 sec for an optional LF
    } else if (c == '\n') {
      goto ok;
    } else {
      // Any other character is stored in the line buffer
      if (_SIM900_bufcnt < SIM900_BUFLEN) {
        _SIM900_buffer[_SIM900_bufcnt++] = c;
      }
    }
  }

  //DIAGPRINTLN("readLine timed out");
  return -1;            // This indicates: timed out

ok:
  _SIM900_buffer[_SIM900_bufcnt] = 0;     // Terminate with NUL byte
  //DIAGPRINT(" "); DIAGPRINTLN(_SIM900_buffer);
  return _SIM900_bufcnt;

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
    if (strcmp(_SIM900_buffer, "OK") == 0) {
      return true;
    }
    // Other input is skipped.
  }
  return false;         // This indicates: timed out
}

bool GPRSbeeClass::waitForMessage(const char *msg, uint32_t ts_max)
{
  int len;
  //DIAGPRINT("waitForMessage: "); DIAGPRINTLN(msg);
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

/*
 * \brief Wait for a prompt, or timeout
 *
 * \return true if succeeded (the reply received), false if otherwise (timed out)
 */
bool GPRSbeeClass::waitForPrompt(const char *prompt, uint32_t ts_max)
{
  const char * ptr = prompt;

  while (*ptr != '\0') {
    if (isTimedOut(ts_max)) {
      break;
    }

    int c = Serial1.read();
    if (c < 0) {
      continue;
    }

    if (c != '\r') {
      //DIAGPRINT((char)c);
    }
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

void GPRSbeeClass::sendCommand(const char *cmd)
{
  delay(500);
  flushInput();
  //DIAGPRINT(">> "); DIAGPRINTLN(cmd);
  _myStream->print(cmd);
  _myStream->print('\r');
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
    *value = strtol(ptr, &bufend, 0);
    if (bufend == ptr) {
      // Invalid number
      return false;
    }
    // Wait for "OK"
    return waitForOK();
  }
  return false;
}

bool GPRSbeeClass::waitForSignalQuality()
{
  // TODO This timeout is maybe too long.
  uint32_t ts_max = millis() + 20000;
  int value;
  while (!isTimedOut(ts_max)) {
    if (getIntValue("AT+CSQ", "+CSQ:", &value, ts_max)) {
      if (value >= _minSignalQuality) {
        return true;
      }
      delay(500);
    }
  }
  return false;
}

bool GPRSbeeClass::waitForCREG()
{
  // TODO This timeout is maybe too long.
  uint32_t ts_max = millis() + 10000;
  int value;
  while (!isTimedOut(ts_max)) {
    sendCommand("AT+CREG?");
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
    if (waitForMessage("+CREG:", ts_max)) {
      const char *ptr = strchr(_SIM900_buffer, ',');
      if (ptr) {
        ++ptr;
        value = strtol(ptr, NULL, 0);
      }
    }
    waitForOK();
    if (value == 1 /* || value == 5 */) {
      return true;
    }
    delay(500);
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

bool GPRSbeeClass::openTCP(const char *apn, const char *server, int port)
{
  uint32_t ts_max;
  boolean retval = false;
  char cmdbuf[60];              // big enough for AT+CIPSTART="TCP","server",8500

  if (!on()) {
    goto ending;
  }

  // Suppress echoing
  if (!sendCommandWaitForOK("ATE0")) {
    goto cmd_error;
  }

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
  if (!sendCommandWaitForOK("AT+CGATT=1", 6000)) {
    goto cmd_error;
  }

  // AT+CSTT=<apn>,<username>,<password>
  strcpy(cmdbuf, "AT+CSTT=\"");
  strcat(cmdbuf, apn);
  strcat(cmdbuf, "\"");
  if (!sendCommandWaitForOK(cmdbuf)) {
    goto cmd_error;
  }

  if (!sendCommandWaitForOK("AT+CIICR")) {
    goto cmd_error;
  }

#if 0
  // Get local IP address
  if (!sendCommandWaitForOK("AT+CIFR")) {
    goto cmd_error;
  }
#endif

  // AT+CIPSHUT
  sendCommand("AT+CIPSHUT");
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage("SHUT OK", ts_max)) {
    goto cmd_error;
  }

  // Start up the connection
  // AT+CIPSTART="TCP","server",8500
  strcpy(cmdbuf, "AT+CIPSTART=\"TCP\",\"");
  strcat(cmdbuf, server);
  strcat(cmdbuf, "\",");
  itoa(port, cmdbuf + strlen(cmdbuf), 10);
  if (!sendCommandWaitForOK(cmdbuf)) {
    goto cmd_error;
  }
  ts_max = millis() + 10000;            // Is this enough?
  if (!waitForMessage("CONNECT OK", ts_max)) {
    // For some weird reason the SIM900 in some cases does not want
    // to give us this CONNECT OK. But then we see it later in the stream.
    //goto cmd_error;
  }

  // AT+CIPQSEND=0  normal send mode (reply after each data send will be SEND OK)
  if (!sendCommandWaitForOK("AT+CIPQSEND=0")) {
    goto cmd_error;
  }

  retval = true;
  goto ending;

cmd_error:
  //DIAGPRINTLN("openTCP failed!");
  off();

ending:
  return retval;
}

void GPRSbeeClass::closeTCP()
{
  uint32_t ts_max;
  // AT+CIPSHUT
  sendCommand("AT+CIPSHUT");
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage("SHUT OK", ts_max)) {
    //DIAGPRINTLN("closeTCP failed!");
  }

  off();
}

/*
 * \brief Send some data over the TCP connection
 */
bool GPRSbeeClass::sendDataTCP(uint8_t *data, int data_len)
{
  uint32_t ts_max;
  bool retval = false;

  delay(500);
  flushInput();
  Serial1.print("AT+CIPSEND=");
  Serial1.println(data_len);
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForPrompt("> ", ts_max)) {
    goto error;
  }
  delay(500);           // Wait a little, just to be sure
  // Send the data
  for (int i = 0; i < data_len; ++i) {
    Serial1.print((char)*data++);
  }
  //
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage("SEND OK", ts_max)) {
    goto error;
  }

  retval = true;
  goto ending;
error:
  //DIAGPRINTLN("sendDataTCP failed!");
ending:
  return retval;
}

bool GPRSbeeClass::receiveLineTCP(char **buffer, uint16_t timeout)
{
  uint32_t ts_max;
  bool retval = false;

  ts_max = millis() + timeout;
  if (readLine(ts_max) < 0) {
    goto ending;
  }
  *buffer = _SIM900_buffer;
  retval = true;

ending:
  return retval;
}
