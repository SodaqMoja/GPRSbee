/*
 * Utils.h
 *
 * This is a collection of miscellaneous functions.
 * They are specifically designed to work with SODAQ
 * boards.  However, with minor modifications they should
 * work on other Arduino boards as well.
 */

#ifndef UTILS_H_
#define UTILS_H_

#include <stddef.h>
#include <stdint.h>
#include <Arduino.h>            // For millis()

uint16_t crc16_ccitt(uint8_t * buf, size_t len);

static inline bool isTimedOut(uint32_t ts)
{
  return (long)(millis() - ts) >= 0;
}

// To compensate for the lack of printf (due to huge increase of memory).
void add0Nd(String &str, uint16_t val, size_t width);
static inline void add04d(String &str, uint16_t val) { add0Nd(str, val, 4); }
static inline void add02d(String &str, uint16_t val) { add0Nd(str, val, 2); }
void add0Nx(String &str, uint16_t val, size_t width);
static inline void add04x(String &str, uint16_t val) { add0Nx(str, val, 4); }
static inline void add02x(String &str, uint16_t val) { add0Nx(str, val, 2); }

uint16_t hex2bin(const char *str, int width);
uint16_t hex2bin(const String &str, int width);

bool getUValue(const char *buffer, uint32_t * value);

#ifdef __cplusplus
extern "C" {
#endif
void fatal(void);
#ifdef __cplusplus
}
#endif

#endif /* UTILS_H_ */
