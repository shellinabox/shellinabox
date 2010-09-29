// logging.c -- Utility functions for managing log messages
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "logging/logging.h"

static int verbosity = MSG_DEFAULT;

static void debugMsg(int level, const char *fmt, va_list ap) {
  if (level <= verbosity) {
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
  }
}

void debug(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugMsg(MSG_DEBUG, fmt, ap);
  va_end(ap);
}

void info(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugMsg(MSG_INFO, fmt, ap);
  va_end(ap);
}

void warn(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugMsg(MSG_WARN, fmt, ap);
  va_end(ap);
}

void error(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugMsg(MSG_ERROR, fmt, ap);
  va_end(ap);
}

void message(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugMsg(MSG_MESSAGE, fmt, ap);
  va_end(ap);
}

void fatal(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debugMsg(MSG_QUIET, fmt, ap);
  va_end(ap);
  _exit(1);
}

int logIsDebug(void) {
  return verbosity >= MSG_DEBUG;
}

int logIsInfo(void) {
  return verbosity >= MSG_INFO;
}

int logIsWarn(void) {
  return verbosity >= MSG_WARN;
}

int logIsError(void) {
  return verbosity >= MSG_ERROR;
}

int logIsMessage(void) {
  return verbosity >= MSG_MESSAGE;
}

int logIsQuiet(void) {
  return verbosity <= MSG_QUIET;
}

int logIsDefault(void) {
  return verbosity == MSG_DEFAULT;
}

int logIsVerbose(void) {
  return verbosity >= MSG_ERROR;
}

void logSetLogLevel(int level) {
  check(level >= MSG_QUIET && level <= MSG_DEBUG);
  verbosity = level;
}

char *vStringPrintf(char *buf, const char *fmt, va_list ap) {
  int offset    = buf ? strlen(buf) : 0;
  int len       = 80;
  check(buf     = realloc(buf, offset + len));
  va_list aq;
  va_copy(aq, ap);
  int p         = vsnprintf(buf + offset, len, fmt, aq);
  va_end(aq);
  if (p >= len) {
    check(buf   = realloc(buf, offset + p + 1));
    va_copy(aq, ap);
    check(vsnprintf(buf + offset, p + 1, fmt, aq) == p);
    va_end(aq);
  } else if (p < 0) {
    int inc     = 256;
    do {
      len      += inc;
      check(len < (1 << 20));
      if (inc < (32 << 10)) {
        inc   <<= 1;
      }
      check(buf = realloc(buf, offset + len));
      va_copy(aq, ap);
      p         = vsnprintf(buf + offset, len, fmt, ap);
      va_end(aq);
    } while (p < 0 || p >= len);
  }
  return buf;
}

char *stringPrintf(char *buf, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char *s = vStringPrintf(buf, fmt, ap);
  va_end(ap);
  return s;
}

char *stringPrintfUnchecked(char *buf, const char *fmt, ...)
#ifdef HAVE_ATTRIBUTE_ALIAS
  __attribute__((alias("stringPrintf")));
#else
{
  va_list ap;
  va_start(ap, fmt);
  char *s = vStringPrintf(buf, fmt, ap);
  va_end(ap);
  return s;
}
#endif

