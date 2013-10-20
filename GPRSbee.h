/*
 * GPRSbee.h
 *
 *  Created on: Oct 19, 2013
 *      Author: Kees Bakker
 */

#ifndef GPRSBEE_H_
#define GPRSBEE_H_

#include <stdint.h>
#include <Arduino.h>
#include <Stream.h>

class GPRSbeeClass
{
public:
  void init(Stream &stream, int ctsPin, int powerPin);
  bool on();
  bool off();

  void setMinSignalQuality(int q) { _minSignalQuality = q; }

  bool openTCP(const char *apn, const char *server, int port);
  void closeTCP();
  bool sendDataTCP(uint8_t *data, int data_len);
  bool receiveLineTCP(char **buffer, uint16_t timeout=2000);

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

#define SIM900_BUFLEN 64
  char _SIM900_buffer[SIM900_BUFLEN + 1];           // +1 for the 0 byte
  int _SIM900_bufcnt;
  Stream *_myStream;
  int _ctsPin;
  int _powerPin;
  int _minSignalQuality;
};

extern GPRSbeeClass gprsbee;

#endif /* GPRSBEE_H_ */
