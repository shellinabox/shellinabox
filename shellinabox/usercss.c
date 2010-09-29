// usercss.c -- Defines user-selectable CSS options
// Copyright (C) 2008-2010 Markus Gutschke <markus@shellinabox.com>
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "logging/logging.h"
#include "shellinabox/usercss.h"
#include "libhttp/hashmap.h"

#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

static struct HashMap *defines;

static void definesDestructor(void *arg ATTR_UNUSED, char *key,
                              char *value ATTR_UNUSED) {
  UNUSED(arg);
  UNUSED(value);

  free(key);
}

static void readStylesheet(struct UserCSS *userCSS, const char *filename,
                           char **style, size_t *len) {
  int fd                  = open(filename, O_RDONLY);
  struct stat st;
  if (fd < 0 || fstat(fd, &st)) {
    fatal("Cannot access style sheet \"%s\"", filename);
  }
  FILE *fp;
  check(fp                = fdopen(fd, "r"));
  check(*style            = malloc(st.st_size + 1));
  if (st.st_size > 0) {
    check(fread(*style, st.st_size, 1, fp) == 1);
  }
  (*style)[st.st_size]    = '\000';
  *len                    = st.st_size;
  fclose(fp);
  if (!memcmp(*style, "/* DEFINES_", 11)) {
    char *e               = strchr(*style + 11, ' ');
    if (e) {
      if (!defines) {
        defines           = newHashMap(definesDestructor, NULL);
      }
      char *def;
      check(def           = malloc(e - *style - 2));
      memcpy(def, *style + 3, e - *style - 3);
      def[e - *style - 3] = '\000';
      addToHashMap(defines, def, (char *)userCSS);
    }
  }
}

void initUserCSS(struct UserCSS *userCSS, const char *arg) {
  userCSS->newGroup                       = 1;

  int numMembers                          = 1;
  int hasActiveMember                     = 0;
  for (;;) {
    const char *colon                     = strchr(arg, ':');
    if (!colon) {
      fatal("Incomplete user CSS definition: \"%s\"", arg);
    }

    check(userCSS->label                  = malloc(6*(colon - arg) + 1));
    for (const char *src = arg, *dst = userCSS->label;;) {
      if (src == colon) {
        *(char *)dst                      = '\000';
        break;
      }
      char ch                             = *src++;
      if (ch == '<') {
        memcpy((char *)dst, "&lt;", 4);
        dst                              += 4;
      } else if (ch == '&') {
        memcpy((char *)dst, "&amp;", 5);
        dst                              += 5;
      } else if (ch == '\'') {
        memcpy((char *)dst, "&apos;", 6);
        dst                              += 6;
      } else if (ch == '"') {
        memcpy((char *)dst, "&quot;", 6);
        dst                              += 6;
      } else {
        *(char *)dst++                    = ch;
      }
    }

    int filenameLen                       = strcspn(colon + 1, ",;");
    char *filename;
    check(filename                        = malloc(filenameLen + 1));
    memcpy(filename, colon + 1, filenameLen);
    filename[filenameLen]                 = '\000';

    switch (*filename) {
      case '-':
        userCSS->isActivated              = 0;
        break;
      case '+':
        if (hasActiveMember) {
          fatal("There can only be one active style option per group. Maybe "
                "use ';' instead of ',' to start a new group.");
        }
        hasActiveMember                   = 1;
        userCSS->isActivated              = 1;
        break;
      default:
        fatal("Must indicate with '+' or '-' whether the style option is "
              "active by default");
    }

    readStylesheet(userCSS, filename + 1, (char **)&userCSS->style,
                   &userCSS->styleLen);
    free(filename);

    arg                                   = colon + 1 + filenameLen;
    if (!*arg) {
      userCSS->next                       = NULL;
      break;
    }
    check(userCSS->next                   = malloc(sizeof(struct UserCSS)));
    userCSS                               = userCSS->next;
    userCSS->newGroup                     = *arg++ == ';';
    if (userCSS->newGroup) {
      if (!hasActiveMember && numMembers > 1) {
        // Print error message
        break;
      }
      numMembers                          = 1;
      hasActiveMember                     = 0;
    } else {
      ++numMembers;
    }
  }
  if (!hasActiveMember && numMembers > 1) {
    fatal("Each group of style options must have exactly one style that is "
          "active by\n"
          "default.");
  }
}

struct UserCSS *newUserCSS(const char *arg) {
  struct UserCSS *userCSS;
  check(userCSS = malloc(sizeof(struct UserCSS)));
  initUserCSS(userCSS, arg);
  return userCSS;
}

void parseUserCSS(struct UserCSS **userCSSList, const char *arg) {
  while (*userCSSList) {
    userCSSList = &(*userCSSList)->next;
  }
  *userCSSList  = newUserCSS(arg);
}

void destroyUserCSS(struct UserCSS *userCSS) {
  if (userCSS) {
    free((void *)userCSS->label);
    userCSS->label         = NULL;
    free((void *)userCSS->style);
    userCSS->style         = NULL;
    userCSS->styleLen      = -1;
    for (struct UserCSS *child = userCSS->next; child; ) {
      struct UserCSS *next = child->next;
      free((void *)child->label);
      free((void *)child->style);
      free(child);
      child                = next;
    }
    userCSS->next          = NULL;
  }
}

void deleteUserCSS(struct UserCSS *userCSS) {
  destroyUserCSS(userCSS);
  free(userCSS);
}

char *getUserCSSString(struct UserCSS *userCSS) {
  char *s   = stringPrintf(NULL, "[ ");
  while (userCSS) {
    s       = stringPrintf(s, "[ '%s', %s, %s ]%s",
                           userCSS->label,
                           userCSS->newGroup    ? "true" : "false",
                           userCSS->isActivated ? "true" : "false",
                           userCSS->next        ? ", "   : "");
    userCSS = userCSS->next;
  }
  return stringPrintf(s, " ]");
}

struct UserCSS *userCSSGetDefine(const char *def) {
  if (!defines) {
    return NULL;
  }
  return (struct UserCSS *)getFromHashMap(defines, def);
}
