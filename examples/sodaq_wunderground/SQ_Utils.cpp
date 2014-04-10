/*
 * Utils.cpp
 *
 * This is a collection of miscellaneous functions.
 * They are specifically designed to work with SODAQ
 * boards.  However, with minor modifications they should
 * work on other Arduino boards as well.
 */


#include <stddef.h>
#include <stdint.h>
#include <util/crc16.h>
#include <Arduino.h>

#include "SQ_Diag.h"
#include "SQ_Utils.h"

#include "pindefs.h"

/*
 * \brief Compute CRC16 of a byte buffer
 */
uint16_t crc16_ccitt(uint8_t * buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc = _crc_ccitt_update(crc, *buf++);
    }
    return crc;
}

/*
 * Format an integer with a certain width
 *
 */

/*
 * Format an integer as %0*d
 *
 * Arduino formatting sucks.
 */
void add0Nd(String &str, uint16_t val, size_t width)
{
  if (width >= 5 && val < 1000) {
    str += '0';
  }
  if (width >= 4 && val < 100) {
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
 * Format an integer as %0*x
 *
 * Arduino formatting sucks.
 */
void add0Nx(String &str, uint16_t val, size_t width)
{
  char buf[5];
  if (width >= 5 && val < 0x1000) {
    str += '0';
  }
  if (width >= 4 && val < 0x100) {
    str += '0';
  }
  if (width >= 3 && val < 0x100) {
    str += '0';
  }
  if (width >= 2 && val < 0x10) {
    str += '0';
  }
  utoa(val, buf, 16);
  str += buf;
}

/*
 * Convert hex digits to a number
 *
 * Isn't there a standard function for this?
 */
uint16_t hex2bin(const char *str, int width)
{
  uint16_t value = 0;
  for (int i = 0; i < width && *str; ++i, ++str) {
    uint8_t b = 0;
    if (*str >= '0' && *str <= '9') {
      b = *str - '0';
    } else if (*str >= 'A' && *str <= 'F') {
      b = *str - 'A' + 10;
    } else if (*str >= 'a' && *str <= 'f') {
      b = *str - 'a' + 10;
    }
    value <<= 4;
    value |= b;
  }
  return value;
}

/*
 * Extract a number from an ASCII string
 */
bool getUValue(const char *buffer, uint32_t * value)
{
  char *eptr;
  if (*buffer == '=') {
    ++buffer;
  }
  *value = strtoul(buffer, &eptr, 0);
  if (eptr != buffer) {
    return true;
  }
  return false;
}

/*
 * Fatal error, panic
 */
void fatal()
{
  showFreeRAM();
  while (true) {
    digitalWrite(FATAL_LED, FATAL_LED_ON);
    delay(1000);
    digitalWrite(FATAL_LED, FATAL_LED_OFF);
    delay(1000);
  }
}
