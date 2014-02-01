#ifndef GPRSBEE_H_
#define GPRSBEE_H_
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

#include <stdint.h>
#include <Arduino.h>
#include <Stream.h>

class GPRSbeeClass
{
public:
  void init(Stream &stream, int ctsPin, int powerPin);
  bool on();
  bool off();
  void setDiag(Stream &stream) { _diagStream = &stream; }
  void setDiag(Stream *stream) { _diagStream = stream; }

  void setMinSignalQuality(int q) { _minSignalQuality = q; }

  bool doHTTPGET(const char *apn, const char *url, char *buffer, size_t len);
  bool doHTTPGET(const char *apn, const char *apnuser, const char *apnpwd,
      const char *url, char *buffer, size_t len);

  bool openTCP(const char *apn, const char *server, int port, bool transMode=false);
  bool openTCP(const char *apn, const char *apnuser, const char *apnpwd,
      const char *server, int port, bool transMode=false);
  void closeTCP();
  bool isTCPConnected();
  bool sendDataTCP(uint8_t *data, int data_len);
  bool receiveLineTCP(char **buffer, uint16_t timeout=2000);

  bool openFTP(const char *apn, const char *server,
      const char *username, const char *password);
  bool openFTP(const char *apn, const char *apnuser, const char *apnpwd,
      const char *server, const char *username, const char *password);
  bool closeFTP();
  bool openFTPfile(const char *fname, const char *path);
  bool sendFTPdata(uint8_t *data, size_t size);
  bool sendFTPdata(uint8_t (*read)(), size_t size);
  bool closeFTPfile();

  bool sendSMS(const char *telno, const char *text);

  bool getIMEI(char *buffer, size_t buflen);
  bool getGCAP(char *buffer, size_t buflen);
  bool getCIMI(char *buffer, size_t buflen);
  bool getCLIP(char *buffer, size_t buflen);
  bool getCLIR(char *buffer, size_t buflen);
  bool getCOLP(char *buffer, size_t buflen);
  bool getCOPS(char *buffer, size_t buflen);

private:
  bool isOn();
  void toggle();
  bool isAlive();
  void switchEchoOff();
  void flushInput();
  int readLine(uint32_t ts_max);
  int readBytes(size_t len, uint8_t *buffer, size_t buflen, uint32_t ts_max);
  bool waitForOK(uint16_t timeout=2000);
  bool waitForMessage(const char *msg, uint32_t ts_max);
  bool waitForMessage_P(const char *msg, uint32_t ts_max);
  int waitForMessages(const char *msgs[], size_t nrMsgs, uint32_t ts_max);
  bool waitForPrompt(const char *prompt, uint32_t ts_max);
  void sendCommand(const char *cmd);
  void sendCommand_P(const char *cmd);
  bool sendCommandWaitForOK(const char *cmd, uint16_t timeout=2000);
  bool sendCommandWaitForOK_P(const char *cmd, uint16_t timeout=2000);
  bool getIntValue(const char *cmd, const char *reply, int * value, uint32_t ts_max);
  bool getStrValue(const char *cmd, const char *reply, char * str, size_t size, uint32_t ts_max);
  bool getStrValue(const char *cmd, char * str, size_t size, uint32_t ts_max);
  bool waitForSignalQuality();
  bool waitForCREG();
  bool setBearerParms(const char *apn, const char *user, const char *pwd);

  // Small utility to see if we timed out
  bool isTimedOut(uint32_t ts) { return (long)(millis() - ts) >= 0; }

  bool sendFTPdata_low(uint8_t *buffer, size_t size);
  bool sendFTPdata_low(uint8_t (*read)(), size_t size);

  void diagPrint(const char *str) { if (_diagStream) _diagStream->print(str); }
  void diagPrintLn(const char *str) { if (_diagStream) _diagStream->println(str); }
  void diagPrint(const __FlashStringHelper *str) { if (_diagStream) _diagStream->print(str); }
  void diagPrintLn(const __FlashStringHelper *str) { if (_diagStream) _diagStream->println(str); }
  void diagPrint(int i, int base=DEC) { if (_diagStream) _diagStream->print(i, base); }
  void diagPrintLn(int i, int base=DEC) { if (_diagStream) _diagStream->println(i, base); }
  void diagPrint(unsigned int i, int base=DEC) { if (_diagStream) _diagStream->print(i, base); }
  void diagPrintLn(unsigned int i, int base=DEC) { if (_diagStream) _diagStream->println(i, base); }
  void diagPrint(unsigned char i, int base=DEC) { if (_diagStream) _diagStream->print(i, base); }
  void diagPrintLn(unsigned char i, int base=DEC) { if (_diagStream) _diagStream->println(i, base); }
  void diagPrint(char c) { if (_diagStream) _diagStream->print(c); }
  void diagPrintLn(char c) { if (_diagStream) _diagStream->println(c); }

#define SIM900_BUFLEN 64
  char _SIM900_buffer[SIM900_BUFLEN + 1];           // +1 for the 0 byte
  int _SIM900_bufcnt;
  Stream *_myStream;
  Stream *_diagStream;
  int _ctsPin;
  int _powerPin;
  int _minSignalQuality;
  size_t _ftpMaxLength;
  bool _transMode;
  bool _echoOff;
};

extern GPRSbeeClass gprsbee;

#endif /* GPRSBEE_H_ */
