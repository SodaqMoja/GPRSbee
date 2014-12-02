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

// Comment this line, or make it an undef to disable
// diagnostic
#define ENABLE_GPRSBEE_DIAG     1

class GPRSbeeClass
{
public:
  void init(Stream &stream, int ctsPin, int powerPin);
  bool on();
  bool off();
#if defined(__AVR_ATmega1284P__)
  void setPowerSwitchedOnOff(bool x) { _onoffMethod = x; }
#endif
  void setDiag(Stream &stream) { _diagStream = &stream; }
  void setDiag(Stream *stream) { _diagStream = stream; }

  void setMinSignalQuality(int q) { _minSignalQuality = q; }

  bool doHTTPGET(const char *apn, const char *url, char *buffer, size_t len);
  bool doHTTPGET(const char *apn, const String & url, char *buffer, size_t len);
  bool doHTTPGET(const char *apn, const char *apnuser, const char *apnpwd,
      const char *url, char *buffer, size_t len);
  bool doHTTPGET1(const char *apn);
  bool doHTTPGET1(const char *apn, const char *apnuser, const char *apnpwd);
  bool doHTTPGET2(const char *url, char *buffer, size_t len);
  void doHTTPGET3();

  bool openTCP(const char *apn, const char *server, int port, bool transMode=false);
  bool openTCP(const char *apn, const char *apnuser, const char *apnpwd,
      const char *server, int port, bool transMode=false);
  void closeTCP();
  bool isTCPConnected();
  bool sendDataTCP(uint8_t *data, int data_len);
  bool receiveLineTCP(const char **buffer, uint16_t timeout=4000);

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
  bool getCCLK(char *buffer, size_t buflen);
  bool getCSPN(char *buffer, size_t buflen);
  bool getCGID(char *buffer, size_t buflen);

  void enableLTS();
  void disableLTS();

private:
  void onToggle();
  void offToggle();
  void onPowerSwitch();
  void offPowerSwitch();
  bool isOn();
  void toggle();
  bool isAlive();
  void switchEchoOff();
  void flushInput();
  int readLine(uint32_t ts_max);
  int readBytes(size_t len, uint8_t *buffer, size_t buflen, uint32_t ts_max);
  bool waitForOK(uint16_t timeout=4000);
  bool waitForMessage(const char *msg, uint32_t ts_max);
  bool waitForMessage_P(const char *msg, uint32_t ts_max);
  int waitForMessages(const char *msgs[], size_t nrMsgs, uint32_t ts_max);
  bool waitForPrompt(const char *prompt, uint32_t ts_max);
  void sendCommandPrepare();
  void sendCommandPartial(const char *cmd);
  void sendCommandPartial_P(const char *cmd);
  void sendCommandNoPrepare(const char *cmd);
  void sendCommandNoPrepare_P(const char *cmd);
  void sendCommand(const char *cmd);
  void sendCommand_P(const char *cmd);
  bool sendCommandWaitForOK(const char *cmd, uint16_t timeout=4000);
  bool sendCommandWaitForOK_P(const char *cmd, uint16_t timeout=4000);
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
#if defined(__AVR_ATmega1284P__)
  bool _onoffMethod;
#endif
};

extern GPRSbeeClass gprsbee;

#endif /* GPRSBEE_H_ */
