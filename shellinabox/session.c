// session.c -- Session management for HTTP/HTTPS connections
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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "shellinabox/session.h"
#include "logging/logging.h"

#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

static HashMap *sessions;


static struct Graveyard {
  struct Graveyard *next;
  time_t           timeout;
  const char       *sessionKey;
} *graveyard;

void addToGraveyard(struct Session *session) {
  // It is possible for a child process to die, but for the Session to
  // linger around, because the browser has also navigated away and thus
  // nobody ever calls completePendingRequest(). We put these Sessions into
  // the graveyard and reap them after a while.
  struct Graveyard *g;
  check(g       = malloc(sizeof(struct Graveyard)));
  g->next       = graveyard;
  g->timeout    = time(NULL) + AJAX_TIMEOUT;
  g->sessionKey = strdup(session->sessionKey);
  graveyard     = g;
}

static void checkGraveyardInternal(int expireAll) {
  if (!graveyard) {
    return;
  }
  time_t timeout = time(NULL) - (expireAll ? 2*AJAX_TIMEOUT : 0);
  for (struct Graveyard **g = &graveyard, *old = *g;
       old; ) {
    if (old->timeout < timeout) {
      *g         = old->next;
      deleteFromHashMap(sessions, old->sessionKey);
      free((char *)old->sessionKey);
      free(old);
    } else {
      g          = &old->next;
    }
    old          = *g;
  }
}

void checkGraveyard(void) {
  checkGraveyardInternal(0);
}

void initSession(struct Session *session, const char *sessionKey,
                 Server *server, URL *url, const char *peerName) {
  session->sessionKey     = sessionKey;
  session->server         = server;
  check(session->peerName = strdup(peerName));
  session->connection     = NULL;
  session->http           = NULL;
  session->url            = url;
  session->done           = 0;
  session->pty            = -1;
  session->width          = 0;
  session->height         = 0;
  session->buffered       = NULL;
  session->len            = 0;
}

struct Session *newSession(const char *sessionKey, Server *server, URL *url,
                           const char *peerName) {
  struct Session *session;
  check(session = malloc(sizeof(struct Session)));
  initSession(session, sessionKey, server, url, peerName);
  return session;
}

void destroySession(struct Session *session) {
  if (session) {
    free((char *)session->peerName);
    free((char *)session->sessionKey);
    deleteURL(session->url);
    if (session->pty >= 0) {
      NOINTR(close(session->pty));
    }
  }
}

void deleteSession(struct Session *session) {
  destroySession(session);
  free(session);
}

void abandonSession(struct Session *session) {
  deleteFromHashMap(sessions, session->sessionKey);
}

void finishSession(struct Session *session) {
  deleteFromHashMap(sessions, session->sessionKey);
}

void finishAllSessions(void) {
  checkGraveyardInternal(1);
  deleteHashMap(sessions);
}

static void destroySessionHashEntry(void *arg ATTR_UNUSED,
                                    char *key ATTR_UNUSED, char *value) {
  UNUSED(arg);
  UNUSED(key);

  deleteSession((struct Session *)value);
}

char *newSessionKey(void) {
  int fd;
  check((fd = NOINTR(open("/dev/urandom", O_RDONLY))) >= 0);
  unsigned char buf[16];
  check(NOINTR(read(fd, buf, sizeof(buf))) == sizeof(buf));
  NOINTR(close(fd));
  char *sessionKey;
  check(sessionKey   = malloc((8*sizeof(buf) + 5)/6 + 1));
  char *ptr          = sessionKey;
  int count          = 0;
  int bits           = 0;
  for (unsigned i = 0;;) {
    bits             = (bits << 8) | buf[i];
    count           += 8;
  drain:
    while (count >= 6) {
      *ptr++         = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef"
                       "ghijklmnopqrstuvwxyz0123456789-/"
                       [(bits >> (count -= 6)) & 0x3F];
    }
    if (++i >= sizeof(buf)) {
      if (count && i == sizeof(buf)) {
        bits       <<= 8;
        count       += 8;
        goto drain;
      } else {
        break;
      }
    }
  }
  *ptr               = '\000';
  check(!sessions || !getFromHashMap(sessions, sessionKey));
  return sessionKey;
}

struct Session *findCGISession(int *isNew, HttpConnection *http, URL *url,
                               const char *cgiSessionKey) {
  *isNew                 = 1;
  if (!sessions) {
    sessions             = newHashMap(destroySessionHashEntry, NULL);
  }
  const HashMap *args    = urlGetArgs(url);
  const char *sessionKey = getFromHashMap(args, "session");
  struct Session *session= NULL;
  if (cgiSessionKey &&
      (!sessionKey || strcmp(cgiSessionKey, sessionKey))) {
    // In CGI mode, we only ever allow exactly one session with a
    // pre-negotiated key.
    deleteURL(url);
  } else {
    if (sessionKey && *sessionKey) {
      session            = (struct Session *)getFromHashMap(sessions,
                                                            sessionKey);
    }
    if (session) {
      *isNew             = 0;
      deleteURL(session->url);
      session->url       = url;
    } else if (!cgiSessionKey && sessionKey && *sessionKey) {
      *isNew             = 0;
      debug("Failed to find session: %s", sessionKey);
      deleteURL(url);
    } else {
      // First contact. Create session, now.
      check(sessionKey   = cgiSessionKey ? strdup(cgiSessionKey)
                                         : newSessionKey());
      session            = newSession(sessionKey, httpGetServer(http), url,
                                      httpGetPeerName(http));
      addToHashMap(sessions, sessionKey, (const char *)session);
      debug("Creating a new session: %s", sessionKey);
    }
  }
  return session;
}

struct Session *findSession(int *isNew, HttpConnection *http, URL *url) {
  return findCGISession(isNew, http, url, NULL);
}

void iterateOverSessions(int (*fnc)(void *, const char *, char **), void *arg){
  iterateOverHashMap(sessions, fnc, arg);
}

int numSessions(void) {
  return getHashmapSize(sessions);
}
