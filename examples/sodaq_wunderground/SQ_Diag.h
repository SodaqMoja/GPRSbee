#ifndef DIAG_H
#define DIAG_H

#include <stdint.h>

/*
 * \brief Define to switch DIAG on of off
 *
 * Comment or make it a #undef
 */
#define ENABLE_DIAG     1

#if ENABLE_DIAG
#include <SoftwareSerial.h>
extern SoftwareSerial diagport;
#define DIAGPRINT(...)          diagport.print(__VA_ARGS__)
#define DIAGPRINTLN(...)        diagport.println(__VA_ARGS__)
void dumpBuffer(const uint8_t * buf, size_t size);
void memoryDump();
void showAddress(const char *txt, void * addr);
void showFreeRAM();
void showBattVolt(float value);

#else
#define DIAGPRINT(...)
#define DIAGPRINTLN(...)
#define dumpBuffer(buf, size)
#define memoryDump()
#define showAddress(txt, addr)
#define showFreeRAM()
#define showBattVolt(value)
#endif


#endif //  DIAG_H
