#ifndef GPRSBEE_H_
#define GPRSBEE_H_
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

  void setMinSignalQuality(int q) { _minSignalQuality = q; }

  bool openTCP(const char *apn, const char *server, int port);
  void closeTCP();
  bool sendDataTCP(uint8_t *data, int data_len);
  bool receiveLineTCP(char **buffer, uint16_t timeout=2000);

  bool openFTP(const char *apn, const char *server, const char *username, const char *password);
  bool closeFTP();
  bool openFTPfile(const char *fname, const char *path);
  bool sendFTPdata(uint8_t *data, size_t size);
  bool closeFTPfile();

private:
  bool isOn();
  void toggle();
  bool isAlive();
  void flushInput();
  int readLine(uint32_t ts_max);
  bool waitForOK(uint16_t timeout=2000);
  bool waitForMessage(const char *msg, uint32_t ts_max);
  bool waitForPrompt(const char *prompt, uint32_t ts_max);
  void sendCommand(const char *cmd);
  bool sendCommandWaitForOK(const char *cmd, uint16_t timeout=2000);
  bool getIntValue(const char *cmd, const char *reply, int * value, uint32_t ts_max);
  bool waitForSignalQuality();
  bool waitForCREG();

  // Small utility to see if we timed out
  bool isTimedOut(uint32_t ts) { return (long)(millis() - ts) >= 0; }

  bool sendFTPdata_low(uint8_t *buffer, size_t size);

  void diagPrint(const char *str) { if (_diagStream) _diagStream->print(str); }
  void diagPrintLn(const char *str) { if (_diagStream) _diagStream->println(str); }
  void diagPrint(char c) { if (_diagStream) _diagStream->print(c); }

#define SIM900_BUFLEN 64
  char _SIM900_buffer[SIM900_BUFLEN + 1];           // +1 for the 0 byte
  int _SIM900_bufcnt;
  Stream *_myStream;
  Stream *_diagStream;
  int _ctsPin;
  int _powerPin;
  int _minSignalQuality;
  size_t _ftpMaxLength;
};

extern GPRSbeeClass gprsbee;

#endif /* GPRSBEE_H_ */
