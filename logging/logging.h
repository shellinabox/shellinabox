// logging.h -- Utility functions for managing log messages
// Copyright (C) 2008-2009 Markus Gutschke <markus@shellinabox.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 2 as
// published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// In addition to these license terms, the author grants the following
// additional rights:
//
// If you modify this program, or any covered work, by linking or
// combining it with the OpenSSL project's OpenSSL library (or a
// modified version of that library), containing parts covered by the
// terms of the OpenSSL or SSLeay licenses, the author
// grants you additional permission to convey the resulting work.
// Corresponding Source for a non-source form of such a combination
// shall include the source code for the parts of OpenSSL used as well
// as that of the covered work.
//
// You may at your option choose to remove this additional permission from
// the work, or from any part of it.
//
// It is possible to build this program in a way that it loads OpenSSL
// libraries at run-time. If doing so, the following notices are required
// by the OpenSSL and SSLeay licenses:
//
// This product includes software developed by the OpenSSL Project
// for use in the OpenSSL Toolkit. (http://www.openssl.org/)
//
// This product includes cryptographic software written by Eric Young
// (eay@cryptsoft.com)
//
//
// The most up-to-date version of this program is always available from
// http://shellinabox.com

#ifndef LOGGING_H__
#define LOGGING_H__

#include <stdarg.h>

#define MSG_QUIET  -1
#define MSG_MESSAGE 0
#define MSG_ERROR   1
#define MSG_WARN    2
#define MSG_INFO    3
#define MSG_DEBUG   4
#define MSG_DEFAULT MSG_ERROR

#define check(x)  do {                                                        \
                    if (!(x))                                                 \
                      fatal("Check failed at "__FILE__":%d in %s(): %s",      \
                             __LINE__, __func__, #x);                         \
                  } while (0)

#define dcheck(x) do {                                                        \
                    if (!(x))                                                 \
                      (logIsDebug() ? fatal : error)(                         \
                            "Check failed at "__FILE__":%d in %s(): %s",      \
                             __LINE__, __func__, #x);                         \
                  } while (0)

void debug(const char *fmt, ...)   __attribute__((format(printf, 1, 2)));
void info(const char *fmt, ...)    __attribute__((format(printf, 1, 2)));
void warn(const char *fmt, ...)    __attribute__((format(printf, 1, 2)));
void error(const char *fmt, ...)   __attribute__((format(printf, 1, 2)));
void message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fatal(const char *fmt, ...)   __attribute__((format(printf, 1, 2),
                                                  noreturn));
int logIsDebug(void);
int logIsInfo(void);
int logIsWarn(void);
int logIsError(void);
int logIsMessage(void);
int logIsQuiet(void);
int logIsDefault(void);
int logIsVerbose(void);
void logSetLogLevel(int level);

char *vStringPrintf(char *buf, const char *fmt, va_list ap);
char *stringPrintf(char *buf, const char *fmt, ...)
  __attribute__((format(printf, 2, 3)));
char *stringPrintfUnchecked(char *buf, const char *fmt, ...);

#endif /* LOGGING_H__ */
