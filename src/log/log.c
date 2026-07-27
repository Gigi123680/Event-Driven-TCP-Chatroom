/**
 * log.c implements a simple logging system with adjustable log levels for
 * different modules. Each module has a log_tag struct with a name and a
 * log_level (ON or OFF). The logd function logs messages to stderr if the
 * module's log level is ON. This allows for easy enabling/disabling of debug
 * output for specific parts of the program. Modules include SHELL, INTERPRETER,
 * SHELLMEMORY, and LOG. This file also hosts useful utility functions for
 * logging.
 */
#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define the log tags for different modules with default level OFF
struct log_tag SHELL = {"SHELL", OFF};
struct log_tag INTERPRETER = {"INTERPRETER", OFF};
struct log_tag SHELLMEMORY = {"SHELLMEMORY", OFF};
struct log_tag LOG = {"LOG", OFF};
struct log_tag CODESTORE = {"CODESTORE", OFF};
struct log_tag PCB_TAG = {"PCB", OFF};
struct log_tag READY_QUEUE = {"READY_QUEUE", OFF};
struct log_tag SCHEDULER = {"SCHEDULER", OFF};
struct log_tag PAGE = {"PAGE", OFF};
struct log_tag EPOLL_TAG = {"EPOLL", ERROR};
struct log_tag PROTOCOL_TAG = {"PROTOCOL", ERROR};
struct log_tag CONNECTION_TAG = {"CONNECTION", ERROR};
struct log_tag CLIENT_TAG = {"CLIENT", ERROR};
struct log_tag SERVER_TAG = {"SERVER", ERROR};

/**
 * Takes a log_tag and a log_level, set that log_tag to that log_level
 */
void logd_level_set(struct log_tag *tag, log_level status) {
  tag->level = status;
}

/**
 * Sets all log levels to OFF
 */
void log_level_set_all_OFF() {
  SHELL.level = OFF;
  INTERPRETER.level = OFF;
  SHELLMEMORY.level = OFF;
  LOG.level = OFF;
  CODESTORE.level = OFF;
  PCB_TAG.level = OFF;
  READY_QUEUE.level = OFF;
  SCHEDULER.level = OFF;
  PAGE.level = OFF;
  EPOLL_TAG.level = OFF;
  PROTOCOL_TAG.level = OFF;
  CONNECTION_TAG.level = OFF;
  CLIENT_TAG.level = OFF;
  SERVER_TAG.level = OFF;
}

bool log_level_enabled(struct log_tag *tag, log_level message_level);

/**
 * Takes a log_tag and a format string with variable arguments, print the
 * formatted string to stderr if the log_tag is ON. Otherwise, do nothing. Each
 * log message is prefixed with the tag name in square brackets.
 */
void logd(struct log_tag *tag, log_level level, const char *fmt, ...) {
  if (!log_level_enabled(tag, level)) {
    return; // specified log level disabled for this tag
  }
  va_list args;
  va_start(args, fmt);

  // debug messages go to stderr
  fprintf(stderr, "[%s] ", tag->TAG); // prefix with tag
  vfprintf(stderr, fmt, args);        // print the formatted string

  va_end(args);
}

/**
 * A variant of logd that does not print the tag prefix.
 * Used for logging multi-part messages where the prefix is only needed once.
 */
void logd_plain(struct log_tag *tag, log_level level, const char *fmt, ...) {
  if (!log_level_enabled(tag, level)) {
    return; // specified log level disabled for this tag
  }
  va_list args;
  va_start(args, fmt);

  // debug messages go to stderr
  vfprintf(stderr, fmt, args); // print the formatted string

  va_end(args);
}

/**
 * Concatenates an array of strings into a single string separated by spaces.
 * Used for debugging and logging purposes.
 */
char *concat(char *words[], int len, int max_len) {
  if (len == 0)
    return NULL;
  char *res =
      malloc(sizeof(char) * max_len); // allocate memory for result string
  strcpy(res, words[0]);
  for (int i = 1; i < len; i++) {
    strcat(res, " ");
    strcat(res, words[i]);
  }
  return res;
}

/**
 * Concatenates an array of strings into a single string separated by spaces,
 * stopping at the first NULL entry.
 * Used for debugging and logging purposes.
 */
char *concat_til_null(char *words[], int max_len) {
  if (words[0] == NULL)
    return NULL;
  char *res =
      malloc(sizeof(char) * max_len); // allocate memory for result string
  strcpy(res, words[0]);
  for (int i = 1; words[i] != NULL; i++) {
    strcat(res, " ");
    strcat(res, words[i]);
  }
  return res;
}

/**
 * Returns true if the message_level should be printed for the tag
 */
bool log_level_enabled(struct log_tag *tag, log_level message_level) {
  return tag->level >= message_level;
}
