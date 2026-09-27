/* ------------------------------ */
//              COG 1
//  @github.com/itskesfet
/* ------------------------------ */

#ifndef COG_H
#define COG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>

#ifndef CLOG_H
#define CLOG_H

typedef enum {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
} LOG_LEVEL;

//----  LOG  ----
#define LOG_LEN 100

//  INTERFACE
void cog(LOG_LEVEL level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

//  Log -> Raise Handler
static void clog_log(LOG_LEVEL level, const char *fmt, va_list args);

typedef void CLog_Handler(
	LOG_LEVEL level,
	const char *message
);

//  Handler {default, file, network socket, database, ...}
void clog_set_handler(CLog_Handler *handler);
void clog_set_level(LOG_LEVEL level);
LOG_LEVEL clog_get_level(void);
void clog_set_terminal_timestamp(bool enabled);

//  Handlers
static void clog_default_handler(
	LOG_LEVEL level,
	const char *message
);

static void clog_terminal_handler(
	LOG_LEVEL level,
	const char *message
);

#ifdef CLOG_IMPLEMENTATION

//  Shared instance per CLOG_IMP..
static CLog_Handler *current_handler;
static LOG_LEVEL clog_level    = LOG_DEBUG;
static bool clog_timestamp     = false;

const char *log_level(LOG_LEVEL level)
{
	switch (level) {
	case LOG_DEBUG:
		return "[DEBUG]";
	case LOG_INFO:
		return "[INFO]";
	case LOG_WARN:
		return "[WARNING]";
	case LOG_ERROR:
		return "[ERROR]";
	default:
		return "[UNSPEC]";
	}
}

void clog_set_level(LOG_LEVEL level)
{
	clog_level = level;
}

LOG_LEVEL clog_get_level(void)
{
	return clog_level;
}

void clog_set_handler(CLog_Handler *handler)
{
	if (!current_handler)
		current_handler = clog_default_handler;

	if (handler)
		current_handler = handler;
}

static void clog_log(
	LOG_LEVEL level,
	const char *fmt,
	va_list args
)
{
	char log_msg[LOG_LEN];
	if (level < clog_level)
		return;
	vsnprintf(log_msg, sizeof(log_msg), fmt, args);

	if (!current_handler)
		current_handler = clog_default_handler;
	current_handler(level, log_msg);
}

static void clog_default_handler(
	LOG_LEVEL level,
	const char *message
)
{
	fprintf(stderr, "%s\n ", message);
}

void clog_set_terminal_timestamp(bool enabled)
{
	clog_timestamp = enabled;
}

static void clog_terminal_handler(
	LOG_LEVEL level,
	const char *message
)
{
	if (clog_timestamp) {
		time_t now = time(NULL);
		struct tm *tm_now = localtime(&now);

		fprintf(stderr,
			"[%02d:%02d:%02d] %s %s\n",
			tm_now->tm_hour,
			tm_now->tm_min,
			tm_now->tm_sec,
			log_level(level),
			message);
	} else {
		fprintf(stderr,
			"%s %s\n",
			log_level(level),
			message);
	}
}

static void clog_null_handler(
	LOG_LEVEL level,
	const char *message
)
{
	(void)level;
	(void)message;
}

void cog(LOG_LEVEL level, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	clog_log(level, fmt, args);
	va_end(args);
}

#endif
#endif

/* ------------------------------ */

#endif
