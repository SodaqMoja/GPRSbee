#include <GPRSbee.h>
#include "Diag.h"

#define APN "yourAPNhere"
#define SERVER "ftp.yourserverhere"
#define USERNAME "username"
#define PASSWORD "password"
#define FTPPATH "/"


#define GPRSBEE_PWRPIN  7
#define XBEECTS_PIN     8

// Only needed if DIAG is enabled
#define DIAGPORT_RX     4
#define DIAGPORT_TX     5

//#########       diag      #############
#ifdef ENABLE_DIAG
#if defined(UBRRH) || defined(UBRR0H)
// There probably is no other Serial port that we can use
// Use a Software Serial instead
#include <SoftwareSerial.h>
SoftwareSerial diagport(DIAGPORT_RX, DIAGPORT_TX);
#else
#define diagport Serial;
#endif
#endif

void setup()
{
  Serial.begin(19200);          // Serial is connected to SIM900 GPRSbee
  gprsbee.init(Serial, XBEECTS_PIN, GPRSBEE_PWRPIN);

#ifdef ENABLE_DIAG
  diagport.begin(9600);
  gprsbee.setDiag(diagport);
#endif
  DIAGPRINTLN("Program start in 4 seconds");
  DIAGPRINTLN("You must disconnect the USB port because it");
  DIAGPRINTLN("uses the same RX/TX as the GPRSbee.");

  delay(4000);
  if (!gprsbee.openFTP(APN, SERVER, USERNAME, PASSWORD)) {
    // Failed to open connection
    return;
  }

  if (!gprsbee.openFTPfile("test_ftp.dat", FTPPATH)) {
    // Failed to open file on FTP server
    gprsbee.off();
  }

  // Append 100 lines with "Hello, world\n" to the FTP file.
  char myData[60];
  strncpy(myData, "Hello, world\n", sizeof(myData));
  for (int i = 0; i < 100; ++i) {
    if (!gprsbee.sendFTPdata((uint8_t *)myData, strlen(myData))) {
      // Failed to upload the data
      gprsbee.off();
      return;
    }
  }

  if (!gprsbee.closeFTPfile()) {
    // Failed to close file. The file upload may still have succeeded.
    gprsbee.off();
    return;
  }

  if (!gprsbee.closeFTP()) {
    // Failed to close the connection. The file upload may still have succeeded.
    gprsbee.off();
    return;
  }

  DIAGPRINTLN("That's all folks.");
}

void loop()
{
  // Do nothing in this simple example.
}

