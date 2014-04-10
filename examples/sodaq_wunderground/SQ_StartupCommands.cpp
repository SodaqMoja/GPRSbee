/*
 * Read commands from the serial line (diagport and/or Serial)
 * and execute them.
 * This can be used to configure the device (and store parameters
 * in EEPROM), or there can be all sorts of debugging commands (if
 * enbled).
 */

#define TIME_FOR_STARTUP_COMMANDS  (20 * 1000)

#include <Arduino.h>
#include <Sodaq_DS3231.h>
#include "SQ_Command.h"
#include "SQ_Diag.h"
#include "SQ_Utils.h"

#include "Config.h"

#include "SQ_StartupCommands.h"

static const Command args[] = {
};

static void showMyCommands(Stream & stream)
{
  size_t nr_cmnds = sizeof(args) / sizeof(args[0]);
  if (nr_cmnds == 0) {
    return;
  }
  stream.println();
  stream.println(F("Commands:"));
  for (size_t i = 0; i < nr_cmnds; ++i) {
    const Command *a = &args[i];
    if (a->show_func) {
      a->show_func(a, stream);
    }
  }
}

/*
 * Execute a command from the commandline
 *
 * Return true if it was a valid command
 */
static bool execCommand(const char * line)
{
  bool done = Command::execCommand(args, sizeof(args) / sizeof(args[0]), line);
  if (done) {
  }
  return done;
}

static int readLine(Stream & stream, char line[], size_t size, uint32_t & ts_max)
{
  int c;
  size_t len = 0;
  bool seenCR = false;
  uint32_t ts_waitLF = 0;
  while (!isTimedOut(ts_max)) {
    if (seenCR) {
      c = stream.peek();
      // ts_waitLF is guaranteed to be non-zero
      if ((c == -1 && isTimedOut(ts_waitLF)) || (c != -1 && c != '\n')) {
        goto end;
      }
      // Only \n should fall through
    }

    c = stream.read();
    if (c < 0) {
      continue;
    }
    ts_max = millis() + TIME_FOR_STARTUP_COMMANDS;
    stream.write((char)c);
    seenCR = c == '\r';
    if (c == '\r') {
      ts_waitLF = millis() + 50;        // Wait another .05 sec for an optional LF
    } else if (c == '\n') {
      goto end;
    } else {
      // Any other character is stored in the line buffer
      if (len < size - 1) {
        line[len++] = c;
      }
    }
  }
  // Timed out. Ignore the input.
  line[0] = '\0';
  return -1;

end:
  line[len] = '\0';
  return len;
}

static void showCommandPrompt(Stream & stream)
{
  showMyCommands(stream);
  ConfigParms::showSettings(stream);
#if ENABLE_DIAG
  showMyCommands(diagport);
  ConfigParms::showSettings(diagport);
#endif
  DIAGPRINT(F("Enter command: "));
  stream.print(F("Enter command: "));
}

/*
 * \brief Read commands from a serial device (can be diagport)
 *
 * \param stream is the Stream from which to read commands
 *
 * Besides from the standard stream we can also read from the diagport
 * if it is enabled
 */
void startupCommands(Stream & stream)
{
  char buffer[64+1];
  int size;
  uint32_t ts_max = millis() + TIME_FOR_STARTUP_COMMANDS;
  bool needPrompt;
  bool seenCommand;

  needPrompt = true;
  while (!isTimedOut(ts_max)) {
    if (needPrompt) {
      showCommandPrompt(stream);
      needPrompt = false;
    }

    size = -1;
    if (stream.available()) {
      size = readLine(stream, buffer, sizeof(buffer), ts_max);
    }
#if ENABLE_DIAG
    if (size <= 0) {
      if (diagport.available()) {
        size = readLine(diagport, buffer, sizeof(buffer), ts_max);
      }
    }
#endif

    if (size < 0) {
      continue;
    }

    needPrompt = true;
    if (size == 0) {
      continue;
    }

    if (strcasecmp_P(buffer, PSTR("ok")) == 0) {
      break;
    }

    seenCommand = false;

    // Is this a command for us?
    if (!seenCommand && execCommand(buffer)) {
      seenCommand = true;
    }
    // Is this a command for EEPROM config?
    if (!seenCommand && parms.execCommand(buffer)) {
      seenCommand = true;
    }

    if (seenCommand) {
      ts_max = millis() + TIME_FOR_STARTUP_COMMANDS;
    }
  }
}
