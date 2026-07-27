#pragma once
/*
 * log.h
 * Logging utilities for the project.
 *
 * This header provides:
 *   - Log levels (ON/OFF) and log tags for different modules.
 *   - Functions to set log levels and log messages for debugging and tracing.
 *
 * Usage:
 *   1. Each module (SHELL, INTERPRETER, SHELLMEMORY, LOG) has a predefined log
 * tag.
 *   2. To enable or disable logging for a module, use logd_level_set:
 *        logd_level_set(&SHELL, ON); // Enable logging for the shell module
 *        logd_level_set(&INTERPRETER, OFF); // Disable logging for interpreter
 *   3. To print a log message (if the tag's level is ON), use logd:
 *        logd(&SHELL, "Shell started with PID %d", getpid());
 *      This prints a formatted message prefixed by the tag name.
 *   4. Only messages for tags with level ON will be printed.
 *
 * Example:
 *   logd_level_set(&SHELL, ON);
 *   logd(&SHELL, "Starting shell...");
 *   logd_level_set(&SHELL, OFF);
 *   logd(&SHELL, "This will not be printed.");
 */
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Log levels: OFF, INFO, ERROR
// ERROR displays the most information
// INFO displays less information
// OFF disables all logging
typedef enum { OFF = 0, INFO = 1, ERROR = 2 } log_level;

// Log tags for different modules
struct log_tag {
  char *TAG;
  log_level level;
};

// Predefined log tags for modules
extern struct log_tag EPOLL_TAG;
extern struct log_tag PROTOCOL_TAG;
extern struct log_tag CONNECTION_TAG;
extern struct log_tag CLIENT_TAG;
extern struct log_tag SERVER_TAG;

// Sets the log level (ON/OFF) for a specific log tag/module.
void logd_level_set(struct log_tag *tag, log_level level);

// Sets all log levels to OFF
void log_level_set_all_OFF();

// Logs a formatted message for a specific log tag/module.
// Usage similar to printf.
void logd(struct log_tag *tag, log_level level, const char *format, ...);

// Logs a formatted message without the tag prefix.
void logd_plain(struct log_tag *tag, log_level level, const char *fmt, ...);

// Concatenates an array of words into a single string separated by spaces.
// Used for logging purposes.
char *concat(char *words[], int len, int max_len);

// Concatenates an array of words until a NULL is encountered.
// Used for logging purposes.
char *concat_til_null(char *words[], int max_len);

#ifdef __cplusplus
}
#endif
