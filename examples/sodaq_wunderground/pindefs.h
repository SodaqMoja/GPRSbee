#ifndef PINDEFS_H
#define PINDEFS_H

//#########   pin definitions SODAQ Moja   ########

#define DF_MOSI         11
#define DF_MISO         12
#define DF_SPICLOCK     13
#define DF_SLAVESELECT  10

#define BATVOLTPIN      A7
#define BATVOLT_R1      10              // in fact 10M
#define BATVOLT_R2      2               // in fact 2M

#define XBEEDTR_PIN     7
#define XBEECTS_PIN     8

#define GROVEPWR_PIN    6
#define GROVEPWR_OFF    LOW
#define GROVEPWR_ON     HIGH

// Only needed if DIAG is enabled
#define DIAGPORT_RX     4       // PD4 Note. No interrupt. Cannot be used for input
#define DIAGPORT_TX     5       // PD5

#define BEEPORT         Serial

#define FATAL_LED       6
#define FATAL_LED_OFF   LOW
#define FATAL_LED_ON    HIGH

#endif // PINDEFS_H
