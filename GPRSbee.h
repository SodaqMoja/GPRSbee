#ifndef GPRSBEE_H_
#define GPRSBEE_H_
/*
 * Copyright (c) 2013-2015 Kees Bakker.  All rights reserved.
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

/*
 * \brief A class to store clock values
 */
class SIMDateTime
{
public:
  SIMDateTime(uint32_t ts=0);
  SIMDateTime(uint8_t y, uint8_t m, uint8_t d, uint8_t hh, uint8_t mm, uint8_t ss, int8_t tz=0);
  SIMDateTime(const char * cclk);

  enum _WEEK_DAYS_ {
    SUNDAY,
    MONDAY,
    TUESDAY,
    WEDNESDAY,
    THURSDAY,
    FRIDAY,
    SATURDAY
  };

  uint16_t      year() { return _yOff + 2000; }
  uint8_t       month() { return _m + 1; }
  uint8_t       day() { return _d + 1; }
  uint8_t       hour() { return _hh; }
  uint8_t       minute() { return _mm; }
  uint8_t       second() { return _ss; }

  // 32-bit number of seconds since Unix epoch (1970-01-01)
  uint32_t getUnixEpoch() const;
  // 32-bit number of seconds since Y2K epoch (2000-01-01)
  uint32_t getY2KEpoch() const;

private:
  uint8_t       conv1d(const char * txt);
  uint8_t       conv2d(const char * txt);

  uint8_t       _yOff;          // Year value minus 2000
  uint8_t       _m;             // month (0..11)
  uint8_t       _d;             // day (0..30)
  uint8_t       _hh;            // hour (0..23)
  uint8_t       _mm;            // minute (0..59)
  uint8_t       _ss;            // second (0..59)
  int8_t        _tz;            // timezone (multiple of 15 minutes)
};

class GPRSbeeClass
{
public:
  void init(Stream &stream, int ctsPin, int powerPin);
  void initNdogoSIM800(Stream &stream, int pwrkeyPin, int vbatPin, int statusPin);
  bool on();
  bool off();
  void setPowerSwitchedOnOff(bool x) { _onoffMethod = onoff_mbili_jp2; }
  void setDiag(Stream &stream) { _diagStream = &stream; }
  void setDiag(Stream *stream) { _diagStream = stream; }

  void setMinSignalQuality(int q) { _minSignalQuality = q; }

  bool doHTTPPOST(const char *apn, const char *url, const char *postdata, size_t pdlen);
  bool doHTTPPOST(const char *apn, const String & url, const char *postdata, size_t pdlen);
  bool doHTTPPOST(const char *apn, const char *apnuser, const char *apnpwd,
      const char *url, const char *postdata, size_t pdlen);
  bool doHTTPPOSTmiddle(const char *url, const char *postdata, size_t pdlen);
  bool doHTTPPOSTmiddleWithReply(const char *url, const char *postdata, size_t pdlen, char *buffer, size_t len);

  bool doHTTPPOSTWithReply(const char *apn, const char *url, const char *postdata, size_t pdlen, char *buffer, size_t len);
  bool doHTTPPOSTWithReply(const char *apn, const String & url, const char *postdata, size_t pdlen, char *buffer, size_t len);
  bool doHTTPPOSTWithReply(const char *apn, const char *apnuser, const char *apnpwd,
      const char *url, const char *postdata, size_t pdlen, char *buffer, size_t len);

  bool doHTTPGET(const char *apn, const char *url, char *buffer, size_t len);
  bool doHTTPGET(const char *apn, const String & url, char *buffer, size_t len);
  bool doHTTPGET(const char *apn, const char *apnuser, const char *apnpwd,
      const char *url, char *buffer, size_t len);
  bool doHTTPGETmiddle(const char *url, char *buffer, size_t len);

  bool doHTTPREAD(char *buffer, size_t len);
  bool doHTTPACTION(char num);

  bool doHTTPprolog(const char *apn);
  bool doHTTPprolog(const char *apn, const char *apnuser, const char *apnpwd);
  void doHTTPepilog();

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
  bool setCIURC(uint8_t value);
  bool getCIURC(char *buffer, size_t buflen);
  bool setCFUN(uint8_t value);
  bool getCFUN(uint8_t * value);

  void enableCIURC();
  void disableCIURC();
  void enableLTS();
  void disableLTS();

  bool sendCommandWaitForOK(const char *cmd, uint16_t timeout=4000);
  bool sendCommandWaitForOK(const String & cmd, uint16_t timeout=4000);
  bool sendCommandWaitForOK_P(const char *cmd, uint16_t timeout=4000);

private:
  void initProlog(Stream &stream);
  void onToggle();
  void offToggle();
  void onSwitchMbiliJP2();
  void offSwitchMbiliJP2();
  void onSwitchNdogoSIM800();
  void offSwitchNdogoSIM800();
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

  void sendCommandProlog();
  void sendCommandAdd(char c);
  void sendCommandAdd(int i);
  void sendCommandAdd(const char *cmd);
  void sendCommandAdd_P(const char *cmd);
  void sendCommandEpilog();

  void sendCommand(const char *cmd);
  void sendCommand_P(const char *cmd);

  bool getIntValue(const char *cmd, const char *reply, int * value, uint32_t ts_max);
  bool getIntValue_P(const char *cmd, const char *reply, int * value, uint32_t ts_max);
  bool getStrValue(const char *cmd, const char *reply, char * str, size_t size, uint32_t ts_max);
  bool getStrValue_P(const char *cmd, const char *reply, char * str, size_t size, uint32_t ts_max);
  bool getStrValue(const char *cmd, char * str, size_t size, uint32_t ts_max);
  bool waitForSignalQuality();
  bool waitForCREG();
  bool setBearerParms(const char *apn, const char *user, const char *pwd);

  // Small utility to see if we timed out
  bool isTimedOut(uint32_t ts) { return (long)(millis() - ts) >= 0; }

  bool sendFTPdata_low(uint8_t *buffer, size_t size);
  bool sendFTPdata_low(uint8_t (*read)(), size_t size);

  enum onoffKind {
    onoff_toggle,
    onoff_mbili_jp2,
    onoff_ndogo_sim800,
  };
#define SIM900_BUFLEN 64
  char _SIM900_buffer[SIM900_BUFLEN + 1];           // +1 for the 0 byte
  int _SIM900_bufcnt;
  Stream *_myStream;
  Stream *_diagStream;
  int8_t _statusPin;
  int8_t _powerPin;
  int8_t _vbatPin;
  int _minSignalQuality;
  size_t _ftpMaxLength;
  bool _transMode;
  bool _echoOff;
  enum onoffKind _onoffMethod;
};

extern GPRSbeeClass gprsbee;

#endif /* GPRSBEE_H_ */
