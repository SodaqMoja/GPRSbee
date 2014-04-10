/*
 * Config.h
 *
 *  Created on: Mar 27, 2014
 *      Author: Kees Bakker
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include <Arduino.h>
#include "SQ_Command.h"
#include "SQ_Diag.h"


class ConfigParms
{
public:
  uint16_t      _b;
  uint32_t      _s;
  char          _stationName[20];
  char          _apn[25];               // Is this enough for the APN?
  char          _pwssrv[40];            // Is this enough for the server name?
  char          _pwsusr[16];            // Is this enough for the server user?
  char          _pwspw[16];             // Is this enough for the server password?
  uint16_t      _pwsport;

public:
  void read();
  void commit(bool forced=false);
  void reset();

  bool execCommand(const char * line);

  uint16_t getB() const { return _b; }
  uint32_t getS() const { return _s; }
  const char *getStationName() const { return _stationName; }
  const char *getAPN() const { return _apn; }
  const char *getPWSserver() const { return _pwssrv; }
  const char *getPWSid() const { return _pwsusr; }
  const char *getPWSpassword() const { return _pwspw; }
  uint16_t getPWSport() const { return _pwsport; }

  static void showSettings(Stream & stream);
  bool checkConfig();

#if ENABLE_DIAG
  void dump();
#else
  void dump() {}
#endif
};

extern ConfigParms      parms;

#endif /* CONFIG_H_ */
