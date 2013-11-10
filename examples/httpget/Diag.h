#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>
#include <SoftwareSerial.h>

/*
 * \brief Define to switch DIAG on of off
 *
 * Uncomment or make it a #undef
 */
#define ENABLE_DIAG     1

#ifdef ENABLE_DIAG
extern SoftwareSerial diagport;
#define DIAGPRINT(...)          diagport.print(__VA_ARGS__)
#define DIAGPRINTLN(...)        diagport.println(__VA_ARGS__)
#else
#define DIAGPRINT(...)
#define DIAGPRINTLN(...)
#endif

void dumpBuffer(uint8_t * buf, size_t size);

#endif //  DIAG_H
