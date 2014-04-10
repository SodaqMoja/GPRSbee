/*
 * SQ_Command.h
 *
 *  Created on: Mar 29, 2014
 *      Author: Kees Bakker
 */

#ifndef SQ_COMMAND_H_
#define SQ_COMMAND_H_

#include <stdint.h>
#include <avr/pgmspace.h>
#include <Arduino.h>


struct Command
{
public:
  const char *name;
  const char *cmd_prefix;
  void (*exec_func)(const Command *a, const char *line);
  void (*show_func)(const Command *a, Stream & stream);
  void *value;
  size_t parm_size;

public:
  static bool execCommand(const Command args[], size_t nr, const char *line);
  static int findCommand(const Command args[], size_t nr, const char *line);

  static void show_name(const Command *s, Stream & stream);
  static void set_string(const Command *a, const char *line);
  static void show_string(const Command *a, Stream & stream);
  static void set_uint16(const Command *a, const char *line);
  static void show_uint16(const Command *a, Stream & stream);
  static void set_uint32(const Command *a, const char *line);
  static void show_uint32(const Command *a, Stream & stream);
};



#endif /* SQ_COMMAND_H_ */
