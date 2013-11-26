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

#include <Arduino.h>
#include <Stream.h>

#include "GPRSbee.h"



GPRSbeeClass gprsbee;

void GPRSbeeClass::init(Stream &stream, int ctsPin, int powerPin)
{
  _myStream = &stream;
  _diagStream = 0;
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

  //diagPrintLn(F("readLine"));
  _SIM900_bufcnt = 0;
  while (!isTimedOut(ts_max)) {
    int c = _myStream->read();
    if (c < 0) {
      if (seenCR && isTimedOut(ts_waitLF)) {
        // Line ended with just <CR>. That's OK too.
        goto ok;
      }
      continue;
    }
    diagPrint((char)c);                 // echo the char
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

int GPRSbeeClass::waitForMessages(const char *msgs[], size_t nrMsgs, uint32_t ts_max)
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
      if (strcmp(_SIM900_buffer, msgs[i]) == 0) {
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

void GPRSbeeClass::sendCommand(const char *cmd)
{
  flushInput();
  diagPrint(F(">> ")); diagPrintLn(cmd);
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

bool GPRSbeeClass::openTCP(const char *apn, const char *server, int port, bool transMode)
{
  uint32_t ts_max;
  boolean retval = false;
  char cmdbuf[60];              // big enough for AT+CIPSTART="TCP","server",8500
  const char *CIPSTART_replies[] = {
		  "CONNECT FAIL",
		  //"STATE: TCP CLOSED",
		  "CONNECT",
  };
  const size_t nrReplies = sizeof(CIPSTART_replies) / sizeof(CIPSTART_replies[0]);
  const int connect_ix = 1;     // This *must* match the index of "CONNECT" above !!!

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
  if (!sendCommandWaitForOK("AT+CISFR")) {
    goto cmd_error;
  }
#endif

  // AT+CIPSHUT
  sendCommand("AT+CIPSHUT");
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage("SHUT OK", ts_max)) {
    goto cmd_error;
  }

  if (transMode) {
    if (!sendCommandWaitForOK("AT+CIPMODE=1")) {
      goto cmd_error;
    }
    //AT+CIPCCFG
    // Read the current settings
    if (!sendCommandWaitForOK("AT+CIPCCFG?")) {
      goto cmd_error;
    }
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
  ts_max = millis() + 15000;            // Is this enough?
  int ix;
  if ((ix = waitForMessages(CIPSTART_replies, nrReplies, ts_max)) < 0) {
    // For some weird reason the SIM900 in some cases does not want
    // to give us this CONNECT OK. But then we see it later in the stream.
    // The manual (V1.03) says that we can expect "CONNECT OK", but so far
    // we have only seen just "CONNECT" (or an error of course).
    goto cmd_error;
  }
  if (ix != connect_ix) {
    // Only CIPSTART_replies[0] is acceptable, i.e. "CONNECT"
    goto cmd_error;
  }

  // AT+CIPQSEND=0  normal send mode (reply after each data send will be SEND OK)
  if (false && !sendCommandWaitForOK("AT+CIPQSEND=0")) {
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
    delay(1000);
    _myStream->print(F("+++"));
    delay(500);
    // TODO Will the SIM900 answer with "OK"?
  }
  sendCommand("AT+CIPSHUT");
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage("SHUT OK", ts_max)) {
    diagPrintLn(F("closeTCP failed!"));
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
  _myStream->print("AT+CIPSEND=");
  _myStream->println(data_len);
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForPrompt("> ", ts_max)) {
    goto error;
  }
  delay(500);           // Wait a little, just to be sure
  // Send the data
  for (int i = 0; i < data_len; ++i) {
    _myStream->print((char)*data++);
  }
  //
  ts_max = millis() + 4000;             // Is this enough?
  if (!waitForMessage("SEND OK", ts_max)) {
    goto error;
  }

  retval = true;
  goto ending;
error:
  diagPrintLn(F("sendDataTCP failed!"));
ending:
  return retval;
}

bool GPRSbeeClass::receiveLineTCP(char **buffer, uint16_t timeout)
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
 * \brief Open a (FTP) session (one file)
 */
bool GPRSbeeClass::openFTP(const char *apn, const char *server, const char *username, const char *password)
{
  char cmd[64];
  int retry;

  if (!on()) {
    goto ending;
  }

  // Suppress echoing
  if (!sendCommandWaitForOK("ATE0")) {
    goto ending;
  }

  // Wait for signal quality
  if (!waitForSignalQuality()) {
    goto ending;
  }

  // Wait for CREG
  if (!waitForCREG()) {
    goto ending;
  }

  // Attach to GPRS service
  // We need a longer timeout than the normal waitForOK
  if (!sendCommandWaitForOK("AT+CGATT=1", 6000)) {
    goto ending;
  }

  // SAPBR=3 Set bearer parameters
  if (!sendCommandWaitForOK("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"")) {
    goto ending;
  }

  // SAPBR=3 Set bearer parameters
  //snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
  strcpy(cmd, "AT+SAPBR=3,1,\"APN\",\"");
  strcat(cmd, apn);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }

  // This command can fail if signal quality is low, or if we're too fast
  for (retry = 0; retry < 5; retry++) {
    if (sendCommandWaitForOK("AT+SAPBR=1,1")) {
      break;
    }
  }
  if (retry >= 5) {
    goto ending;
  }

  if (!sendCommandWaitForOK("AT+SAPBR=2,1")) {
    goto ending;
  }

  if (!sendCommandWaitForOK("AT+FTPCID=1")) {
    goto ending;
  }

  // connect to FTP server
  //snprintf(cmd, sizeof(cmd), "AT+FTPSERV=\"%s\"", server);
  strcpy(cmd, "AT+FTPSERV=\"");
  strcat(cmd, server);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }

  // optional "AT+FTPPORT=21";
  //snprintf(cmd, sizeof(cmd), "AT+FTPUN=\"%s\"", username);
  strcpy(cmd, "AT+FTPUN=\"");
  strcat(cmd, username);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }
  //snprintf(cmd, sizeof(cmd), "AT+FTPPW=\"%s\"", password);
  strcpy(cmd, "AT+FTPPW=\"");
  strcat(cmd, password);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }

  return true;

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
  strcpy(cmd, "AT+FTPPUTNAME=\"");
  strcat(cmd, fname);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUTPATH=\"%s\"", FTPPATH);
  strcpy(cmd, "AT+FTPPUTPATH=\"");
  strcat(cmd, path);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto ending;
  }

  // Repeat until we get OK
  for (retry = 0; retry < 5; retry++) {
    if (sendCommandWaitForOK("AT+FTPPUT=1")) {
      break;
    }
  }
  if (retry >= 5) {
    goto ending;
  }

  // +FTPPUT:1,1,1360  <= the 1360 is <maxlength>
  // +FTPPUT:1,66      <= this is an error
  ts_max = millis() + 6000;
  if (!waitForMessage("+FTPPUT:1,", ts_max)) {
    goto ending;
  }
  if (strncmp(_SIM900_buffer + 10, "1,", 2) != 0) {
    // We did NOT get "+FTPPUT:1,1,", it might be an error.
    // Sometimes we see:
    //    +FTPPUT:1,66
    // The doc says that 66 is the error, but 66 is not documented
    goto ending;
  }
  _ftpMaxLength = strtol(_SIM900_buffer + 12, NULL, 0);

  return true;

ending:
  return false;
}

bool GPRSbeeClass::closeFTPfile()
{
  // Close file
  if (!sendCommandWaitForOK("AT+FTPPUT=2,0")) {
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
  uint32_t ts_max = millis() + 10000;
  if (!waitForMessage("+FTPPUT:1,", ts_max)) {
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
  char cmd[64];
  uint32_t ts_max;
  uint8_t *ptr = buffer;

  // Send some data
  //snprintf(cmd, sizeof(cmd), "AT+FTPPUT=2,%d", size);
  strcpy(cmd, "AT+FTPPUT=2,");
  itoa(size, cmd + strlen(cmd), 10);
  _myStream->print(cmd);
  _myStream->print('\r');
  delay(500);           // TODO Find out if we can drop this

  ts_max = millis() + 4000;
  // +FTPPUT:2,22
  if (!waitForMessage("+FTPPUT:2,", ts_max)) {
    // How bad is it if we ignore this
  }

  // Send data ...
  for (size_t i = 0; i < size; ++i) {
    _myStream->print((char)*ptr++);
  }
  //_myStream->print('\r');          // dummy <CR>, not sure if this is needed

  // Expected reply:
  // +FTPPUT:2,22
  // OK
  // +FTPPUT:1,1,1360

  if (!waitForOK()) {
    return false;
  }
  ts_max = millis() + 4000;
  // +FTPPUT:1,1,1360
  if (!waitForMessage("+FTPPUT:1,", ts_max)) {
    // How bad is it if we ignore this
  }

  return true;
}

bool GPRSbeeClass::sendFTPdata(uint8_t *data, size_t size)
{
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

bool GPRSbeeClass::sendSMS(const char *telno, const char *text)
{
  char cmd[64];
  uint32_t ts_max;
  bool retval = false;

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
  if (!sendCommandWaitForOK("AT+CMGF=1")) {
    goto cmd_error;
  }

  strcpy(cmd, "AT+CMGS=\"");
  strcat(cmd, telno);
  strcat(cmd, "\"");
  sendCommand(cmd);
  ts_max = millis() + 4000;
  if (!waitForPrompt("> ", ts_max)) {
    goto cmd_error;
  }
  _myStream->print(text); //the message itself
  _myStream->print((char)26); //the ASCII code of ctrl+z is 26, this is needed to end the send modus and send the message.
  if (!waitForOK(20000)) {
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
  char cmd[128];
  int retry;
  uint32_t ts_max;
  size_t getLength = 0;
  int i;
  bool retval = false;

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

  // SAPBR=3 Set bearer parameters
  if (!sendCommandWaitForOK("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"")) {
    goto cmd_error;
  }

  // SAPBR=3 Set bearer parameters
  //snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
  strcpy(cmd, "AT+SAPBR=3,1,\"APN\",\"");
  strcat(cmd, apn);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto cmd_error;
  }

  // SAPBR=1 Open bearer
  // This command can fail if signal quality is low, or if we're too fast
  for (retry = 0; retry < 5; retry++) {
    if (sendCommandWaitForOK("AT+SAPBR=1,1")) {
      break;
    }
  }
  if (retry >= 5) {
    goto cmd_error;
  }

  // SAPBR=2 Query bearer
  // Expect +SAPBR: <cid>,<Status>,<IP_Addr>
  if (!sendCommandWaitForOK("AT+SAPBR=2,1")) {
    goto cmd_error;
  }

  // initialize http service
  if (!sendCommandWaitForOK("AT+HTTPINIT")) {
    goto cmd_error;
  }

  // set http param CID value
  // FIXME Do we need this?

  // set http param URL value
  strcpy(cmd, "AT+HTTPPARA=\"URL\",\"");
  if (strlen(cmd) + strlen(url) + 2 > sizeof(cmd)) {
    // Buffer overflow
    goto cmd_error;
  }
  strcat(cmd, url);
  strcat(cmd, "\"");
  if (!sendCommandWaitForOK(cmd)) {
    goto cmd_error;
  }

  // set http action type 0 = GET, 1 = POST, 2 = HEAD
  if (!sendCommandWaitForOK("AT+HTTPACTION=0")) {
    goto cmd_error;
  }
  // Now we're expecting something like this: +HTTPACTION: <Method>,<StatusCode>,<DataLen>
  // <Method> 0
  // <StatusCode> 200
  // <DataLen> ??
  ts_max = millis() + 4000;
  if (waitForMessage("+HTTPACTION:", ts_max)) {
    // TODO Check for StatusCode 200
    // TODO Check for DataLen
  }

  // Read all data
  // Expect
  //   +HTTPREAD:<date_len>
  //   <data>
  //   OK
  sendCommand("AT+HTTPREAD");
  ts_max = millis() + 8000;
  if (waitForMessage("+HTTPREAD:", ts_max)) {
    const char *ptr = _SIM900_buffer + 10;
    char *bufend;
    getLength = strtol(ptr, &bufend, 0);
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

  if (!sendCommandWaitForOK("AT+HTTPTERM")) {
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
