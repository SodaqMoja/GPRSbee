#include <stddef.h>
#include <stdint.h>

#include "pindefs.h"
#include "SQ_Diag.h"


#ifdef ENABLE_DIAG
#include <SoftwareSerial.h>
SoftwareSerial diagport(DIAGPORT_RX, DIAGPORT_TX);

// Silly Arduino Print has very limited formatting capabilities
static void print04x(uint16_t val)
{
  if (val < 0x1000) {
    DIAGPRINT('0');
  }
  if (val < 0x100) {
    DIAGPRINT('0');
  }
  if (val < 0x10) {
    DIAGPRINT('0');
  }
  DIAGPRINT(val, HEX);
}
static void print0x04x(uint16_t val)
{
  DIAGPRINT(F("0x"));
  print04x(val);
}
static void print02x(uint8_t val)
{
  if (val < 0x10) {
    DIAGPRINT('0');
  }
  DIAGPRINT(val, HEX);
}
static void print0x02x(uint16_t val)
{
  DIAGPRINT(F("0x"));
  print02x(val);
}

void dumpBuffer(const uint8_t * buf, size_t size)
{
  while (size > 0) {
    size_t size1 = size >= 16 ? 16 : size;
    for (size_t j = 0; j < size1; j++) {
      print02x(*buf++);
    }
    DIAGPRINTLN();
    size -= size1;
  }
}

void showBattVolt(float value)
{
  DIAGPRINT(F("Battery Voltage: ")); DIAGPRINT((int)(value * 1000)); DIAGPRINTLN(F("mV"));
}

void memoryDump()
{
  const size_t chunk = 256;
  const size_t size = 2048;
  extern void * __data_start;
  const uint8_t * start = (const uint8_t *)&__data_start;
  for (const uint8_t * addr = start; addr < (uint8_t *)(start + size); addr += chunk) {
    print0x04x((uint16_t)addr);
    DIAGPRINTLN();
    dumpBuffer(addr, chunk);
  }
}

void showAddress(const char *txt, void *addr)
{
  DIAGPRINT(txt); print0x04x((uint16_t)addr); DIAGPRINTLN();
}

//=====================================================================================
// Returns the number of bytes currently free in RAM
//=====================================================================================
static int inline freeRAM(uint16_t *topOfStack)
{
  extern uint16_t  __bss_end;
  extern uint16_t* __brkval;

  if (reinterpret_cast<int>(__brkval) == 0)
  {
    // if no heap use from end of bss section
    //=======================================
    return reinterpret_cast<int>(topOfStack) - reinterpret_cast<int>(&__bss_end);
  }

  // use from top of stack to heap
  //==============================
  return reinterpret_cast<int>(topOfStack) - reinterpret_cast<int>(__brkval);
}

//=====================================================================================
// show amount of RAM free
//=====================================================================================
void showFreeRAM()
{
  extern uint16_t  __bss_end;
  extern void * __brkval;
  uint16_t stackVar;
  DIAGPRINT(F("free RAM: ")); DIAGPRINT(freeRAM(&stackVar)); DIAGPRINTLN(F(" bytes "));
  DIAGPRINT(F("    __bss_end: ")); print0x04x((uint16_t)&__bss_end); DIAGPRINTLN();
  DIAGPRINT(F("     __brkval: ")); print0x04x((uint16_t)__brkval); DIAGPRINTLN();
  DIAGPRINT(F(" stackpointer: ")); print0x04x((uint16_t)AVR_STACK_POINTER_REG); DIAGPRINTLN();
}
#endif
