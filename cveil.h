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

#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

/* ------------------------------ */
// CLOG provides the library basic logging system.
#ifndef CLOG_H
#define CLOG_H

typedef enum {
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR
} LOG_LEVEL;

//			  LOG  
#define LOG_LEN 100

//  INTERFACE
void cog(LOG_LEVEL level, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
typedef void CLog_Handler(
	LOG_LEVEL level,
	const char *message
);

//  Handler {default, file, network socket, database, ...}
void clog_set_handler(CLog_Handler *handler);
void clog_set_level(LOG_LEVEL level);
LOG_LEVEL clog_get_level(void);
void clog_set_terminal_timestamp(bool enabled);

#ifdef CLOG_IMPLEMENTATION

//  Shared instance per CLOG_IMP..
static CLog_Handler *current_handler;
static LOG_LEVEL clog_level    = LOG_DEBUG;
static bool clog_timestamp     = false;

static const char *log_level(LOG_LEVEL level)
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
//  Handlers
static void clog_default_handler(
	LOG_LEVEL level,
	const char *message
);

static void clog_terminal_handler(
	LOG_LEVEL level,
	const char *message
);
//----
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
	(void) level;
	fprintf(stderr, "%s\n", message);
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
// CBuild provides a simple interface for building and running programs

#ifndef CBUILD_H
#define CBUILD_H

typedef struct {
    const char **cmd;
    size_t count;
    size_t capacity;
} CMD_ARG;

typedef struct {
    const char *name;
    CMD_ARG cmd;
} Build;

int build_program(const char **arg);

#ifdef CBUILD_IMPLEMENTATION

static Build build_struct(const char *name);
static Build build_struct(const char *name)
{
    Build build = {0};

    if (name == NULL)
        return build;

    build.name = name;
    return build;
}

static int construct_program(Build *build)
{
    if (build == NULL ||
        build->cmd.cmd == NULL ||
        build->cmd.cmd[0] == NULL)
        return -1;

    pid_t pid = fork();

    if (pid < 0)
        return -1;

    if (pid == 0) {
        execvp(build->cmd.cmd[0],
               (char * const *)build->cmd.cmd);
        _exit(127);
    }
	int status;

	if (waitpid(pid, &status, 0) < 0)
	    return -1;

	if (WIFEXITED(status))
	    return WEXITSTATUS(status);

	if (WIFSIGNALED(status))
	    return 128 + WTERMSIG(status);

	return -1;
}

int build_program(const char **arg)
{
    if (arg == NULL || arg[0] == NULL)
        return -1;

    Build build = build_struct(arg[0]);
    build.cmd.cmd = arg;

    for (size_t i = 0; arg[i] != NULL; ++i)
        build.cmd.count++;

    return construct_program(&build);
}

#endif 
#endif 


/* ------------------------------ */

#endif
