// shellinaboxd.c -- A custom web server that makes command line applications
//                   available as AJAX web applications.
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

#define _GNU_SOURCE
#include "config.h"

#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "libhttp/http.h"
#include "logging/logging.h"
#include "shellinabox/externalfile.h"
#include "shellinabox/launcher.h"
#include "shellinabox/privileges.h"
#include "shellinabox/service.h"
#include "shellinabox/session.h"
#include "shellinabox/usercss.h"

#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

// Embedded resources
#include "shellinabox/beep.h"
#include "shellinabox/cgi_root.h"
#include "shellinabox/enabled.h"
#include "shellinabox/favicon.h"
#include "shellinabox/keyboard.h"
#include "shellinabox/keyboard-layout.h"
#include "shellinabox/print-styles.h"
#include "shellinabox/root_page.h"
#include "shellinabox/shell_in_a_box.h"
#include "shellinabox/styles.h"
#include "shellinabox/vt100.h"

#define PORTNUM           4200
#define MAX_RESPONSE      2048

static int            port;
static int            portMin;
static int            portMax;
static int            localhostOnly = 0;
static int            noBeep        = 0;
static int            numericHosts  = 0;
static int            enableSSL     = 1;
static int            enableSSLMenu = 1;
static int            linkifyURLs   = 1;
static char           *certificateDir;
static int            certificateFd = -1;
static HashMap        *externalFiles;
static Server         *cgiServer;
static char           *cgiSessionKey;
static int            cgiSessions;
static char           *cssStyleSheet;
static struct UserCSS *userCSSList;
static const char     *pidfile;
static sigjmp_buf     jmpenv;
static volatile int   exiting;

static char *jsonEscape(const char *buf, int len) {
  static const char *hexDigit = "0123456789ABCDEF";

  // Determine the space that is needed to encode the buffer
  int count                   = 0;
  const char *ptr             = buf;
  for (int i = 0; i < len; i++) {
    unsigned char ch          = *(unsigned char *)ptr++;
    if (ch < ' ') {
      switch (ch) {
      case '\b': case '\f': case '\n': case '\r': case '\t':
        count                += 2;
        break;
      default:
        count                += 6;
        break;
      }
    } else if (ch == '"' || ch == '\\' || ch == '/') {
      count                  += 2;
    } else if (ch > '\x7F') {
      count                  += 6;
    } else {
      count++;
    }
  }

  // Encode the buffer using JSON string escaping
  char *result;
  check(result                = malloc(count + 1));
  char *dst                   = result;
  ptr                         = buf;
  for (int i = 0; i < len; i++) {
    unsigned char ch          = *(unsigned char *)ptr++;
    if (ch < ' ') {
      *dst++                  = '\\';
      switch (ch) {
      case '\b': *dst++       = 'b'; break;
      case '\f': *dst++       = 'f'; break;
      case '\n': *dst++       = 'n'; break;
      case '\r': *dst++       = 'r'; break;
      case '\t': *dst++       = 't'; break;
      default:
      unicode:
        *dst++                = 'u';
        *dst++                = '0';
        *dst++                = '0';
        *dst++                = hexDigit[ch >> 4];
        *dst++                = hexDigit[ch & 0xF];
        break;
      }
    } else if (ch == '"' || ch == '\\' || ch == '/') {
      *dst++                  = '\\';
      *dst++                  = ch;
    } else if (ch > '\x7F') {
      *dst++                  = '\\';
      goto unicode;
    } else {
      *dst++                  = ch;
    }
  }
  *dst++                      = '\000';
  return result;
}

static int printfUnchecked(const char *format, ...) {
  // Some Linux distributions enable -Wformat=2 by default. This is a
  // very unfortunate decision, as that option generates a lot of false
  // positives. We try to work around the problem by defining an unchecked
  // version of "printf()"
  va_list ap;
  va_start(ap, format);
  int rc = vprintf(format, ap);
  va_end(ap);
  return rc;
}

static int completePendingRequest(struct Session *session,
                                  const char *buf, int len, int maxLength) {
  // If there is no pending HTTP request, save the data and return
  // immediately.
  if (!session->http) {
    if (len) {
      if (session->buffered) {
        check(session->buffered = realloc(session->buffered,
                                          session->len + len));
        memcpy(session->buffered + session->len, buf, len);
        session->len           += len;
      } else {
        check(session->buffered = malloc(len));
        memcpy(session->buffered, buf, len);
        session->len            = len;
      }
    }
  } else {
    // If we have a pending HTTP request, we can reply to it, now.
    char *data;
    if (session->buffered) {
      check(session->buffered   = realloc(session->buffered,
                                          session->len + len));
      memcpy(session->buffered + session->len, buf, len);
      session->len             += len;
      if (maxLength > 0 && session->len > maxLength) {
        data                    = jsonEscape(session->buffered, maxLength);
        session->len           -= maxLength;
        memmove(session->buffered, session->buffered + maxLength,
                session->len);
      } else {
        data                    = jsonEscape(session->buffered, session->len);
        free(session->buffered);
        session->buffered       = NULL;
        session->len            = 0;
      }
    } else {
      if (maxLength > 0 && len > maxLength) {
        session->len            = len - maxLength;
        check(session->buffered = malloc(session->len));
        memcpy(session->buffered, buf + maxLength, session->len);
        data                    = jsonEscape(buf, maxLength);
      } else {
        data                    = jsonEscape(buf, len);
      }
    }
    
    char *json                  = stringPrintf(NULL, "{"
                                               "\"session\":\"%s\","
                                               "\"data\":\"%s\""
                                               "}",
                                               session->sessionKey, data);
    free(data);
    HttpConnection *http        = session->http;
    char *response              = stringPrintf(NULL,
                                             "HTTP/1.1 200 OK\r\n"
                                             "Content-Type: application/json; "
                                             "charset=utf-8\r\n"
                                             "Content-Length: %ld\r\n"
                                             "Cache-Control: no-cache\r\n"
                                             "\r\n"
                                             "%s",
                                             (long)strlen(json),
                                             strcmp(httpGetMethod(http),
                                                    "HEAD") ? json : "");
    free(json);
    session->http               = NULL;
    httpTransfer(http, response, strlen(response));
  }
  if (session->done && !session->buffered) {
    finishSession(session);
    return 0;
  }
  return 1;
}

static void sessionDone(void *arg) {
  debug("Child terminated");
  struct Session *session = (struct Session *)arg;
  session->done           = 1;
  addToGraveyard(session);
  completePendingRequest(session, "", 0, INT_MAX);
}

static int handleSession(struct ServerConnection *connection, void *arg,
                         short *events, short revents) {
  struct Session *session       = (struct Session *)arg;
  session->connection           = connection;
  int len                       = MAX_RESPONSE - session->len;
  if (len <= 0) {
    len                         = 1;
  }
  char buf[MAX_RESPONSE];
  int bytes                     = 0;
  if (revents & POLLIN) {
    bytes                       = NOINTR(read(session->pty, buf, len));
    if (bytes <= 0) {
      return 0;
    }
  }
  int timedOut                  = serverGetTimeout(connection) < 0;
  if (bytes || timedOut) {
    if (!session->http && timedOut) {
      debug("Timeout. Closing session.");
      return 0;
    }
    check(!session->done);
    check(completePendingRequest(session, buf, bytes, MAX_RESPONSE));
    connection                  = serverGetConnection(session->server,
                                                      connection,
                                                      session->pty);
    session->connection         = connection;
    if (session->len >= MAX_RESPONSE) {
      *events                   = 0;
    }
    serverSetTimeout(connection, AJAX_TIMEOUT);
    return 1;
  } else {
    return 0;
  }
}

static int invalidatePendingHttpSession(void *arg, const char *key,
                                        char **value) {
  struct Session *session = *(struct Session **)value;
  if (session->http && session->http == (HttpConnection *)arg) {
    debug("Clearing pending HTTP connection for session %s", key);
    session->http         = NULL;
    serverDeleteConnection(session->server, session->pty);

    // Return zero in order to remove this HTTP from the "session" hashmap
    return 0;
  }

  // If the session is still in use, do not remove it from the "sessions" map
  return 1;
}

static int dataHandler(HttpConnection *http, struct Service *service,
                       const char *buf, int len ATTR_UNUSED, URL *url) {
  UNUSED(len);
  if (!buf) {
    // Somebody unexpectedly closed our http connection (e.g. because of a
    // timeout). This is the last notification that we will get.
    deleteURL(url);
    iterateOverSessions(invalidatePendingHttpSession, http);
    return HTTP_DONE;
  }

  // Find an existing session, or create the record for a new one
  int isNew;
  struct Session *session = findCGISession(&isNew, http, url, cgiSessionKey);
  if (session == NULL) {
    httpSendReply(http, 400, "Bad Request", NO_MSG);
    return HTTP_DONE;
  }

  // Sanity check
  if (!isNew && strcmp(session->peerName, httpGetPeerName(http))) {
    error("Peername changed from %s to %s",
          session->peerName, httpGetPeerName(http));
    httpSendReply(http, 400, "Bad Request", NO_MSG);
    return HTTP_DONE;
  }

  const HashMap *args     = urlGetArgs(session->url);
  int oldWidth            = session->width;
  int oldHeight           = session->height;
  const char *width       = getFromHashMap(args, "width");
  const char *height      = getFromHashMap(args, "height");
  const char *keys        = getFromHashMap(args, "keys");
  const char *rootURL     = getFromHashMap(args, "rooturl");

  // Adjust window dimensions if provided by client
  if (width && height) {
    session->width        = atoi(width);
    session->height       = atoi(height);
  }

  // Create a new session, if the client did not provide an existing one
  if (isNew) {
    if (keys) {
    bad_new_session:
      abandonSession(session);
      httpSendReply(http, 400, "Bad Request", NO_MSG);
      return HTTP_DONE;
    }

    if (cgiServer && cgiSessions++) {
      serverExitLoop(cgiServer, 1);
      goto bad_new_session;
    }
    session->http         = http;
    if (launchChild(service->id, session,
                    rootURL && *rootURL ? rootURL : urlGetURL(url)) < 0) {
      abandonSession(session);
      httpSendReply(http, 500, "Internal Error", NO_MSG);
      return HTTP_DONE;
    }
    if (cgiServer) {
      terminateLauncher();
    }
    session->connection   = serverAddConnection(httpGetServer(http),
                                                session->pty, handleSession,
                                                sessionDone, session);
    serverSetTimeout(session->connection, AJAX_TIMEOUT);
  }

  // Reset window dimensions of the pseudo TTY, if changed since last time set.
  if (session->width > 0 && session->height > 0 &&
      (session->width != oldWidth || session->height != oldHeight)) {
    debug("Window size changed to %dx%d", session->width, session->height);
    setWindowSize(session->pty, session->width, session->height);
  }

  // Process keypresses, if any. Then send a synchronous reply.
  if (keys) {
    char *keyCodes;
    check(keyCodes        = malloc(strlen(keys)/2));
    int len               = 0;
    for (const unsigned char *ptr = (const unsigned char *)keys; ;) {
      unsigned c0         = *ptr++;
      if (c0 < '0' || (c0 > '9' && c0 < 'A') ||
          (c0 > 'F' && c0 < 'a') || c0 > 'f') {
        break;
      }
      unsigned c1         = *ptr++;
      if (c1 < '0' || (c1 > '9' && c1 < 'A') ||
          (c1 > 'F' && c1 < 'a') || c1 > 'f') {
        break;
      }
      keyCodes[len++]     = 16*((c0 & 0xF) + 9*(c0 > '9')) +
                                (c1 & 0xF) + 9*(c1 > '9');
    }
    if (write(session->pty, keyCodes, len) < 0 && errno == EAGAIN) {
      completePendingRequest(session, "\007", 1, MAX_RESPONSE);
    }
    free(keyCodes);
    httpSendReply(http, 200, "OK", " ");
    check(session->http != http);
    return HTTP_DONE;
  } else {
    // This request is polling for data. Finish any pending requests and
    // queue (or process) a new one.
    if (session->http && session->http != http &&
        !completePendingRequest(session, "", 0, MAX_RESPONSE)) {
      httpSendReply(http, 400, "Bad Request", NO_MSG);
      return HTTP_DONE;
    }
    session->http         = http;
  }

  session->connection     = serverGetConnection(session->server,
                                                session->connection,
                                                session->pty);
  if (session->buffered || isNew) {
    if (completePendingRequest(session, "", 0, MAX_RESPONSE) &&
        session->connection) {
      // Reset the timeout, as we just received a new request.
      serverSetTimeout(session->connection, AJAX_TIMEOUT);
      if (session->len < MAX_RESPONSE) {
        // Re-enable input on the child's pty
        serverConnectionSetEvents(session->server, session->connection,
                                  session->pty, POLLIN);
      }
    }
    return HTTP_DONE;
  } else if (session->connection) {
    // Re-enable input on the child's pty
    serverConnectionSetEvents(session->server, session->connection,
                              session->pty, POLLIN);
    serverSetTimeout(session->connection, AJAX_TIMEOUT);
  }

  return HTTP_SUSPEND;
}

static void serveStaticFile(HttpConnection *http, const char *contentType,
                            const char *start, const char *end) {
  char *body                     = (char *)start;
  char *bodyEnd                  = (char *)end;

  // Unfortunately, there are still some browsers that are so buggy that they
  // need special conditional code. In anything that has a "text" MIME type,
  // we allow simple conditionals. Nested conditionals are not supported.
  if (!memcmp(contentType, "text/", 5)) {
    char *tag                    = NULL;
    int condTrue                 = -1;
    char *ifPtr                  = NULL;
    char *elsePtr                = NULL;
    for (char *ptr = body; bodyEnd - ptr >= 6; ) {
      char *eol                  = ptr;
      eol                        = memchr(eol, '\n', bodyEnd - eol);
      if (eol == NULL) {
        eol                      = bodyEnd;
      } else {
        ++eol;
      }
      if (!memcmp(ptr, "[if ", 4)) {
        char *bracket            = memchr(ptr + 4, ']', eol - ptr - 4);
        if (bracket != NULL && bracket > ptr + 4) {
          check(tag              = malloc(bracket - ptr - 3));
          memcpy(tag, ptr + 4, bracket - ptr - 4);
          tag[bracket - ptr - 4] = '\000';
          condTrue               = 0;
          const char *userAgent  = getFromHashMap(httpGetHeaders(http),
                                                  "user-agent");
          if (!userAgent) {
            userAgent            = "";
          }

          // Allow multiple comma separated conditions. Conditions are either
          // substrings found in the user agent, or they are "DEFINES_..."
          // tags at the top of user CSS files.
          for (char *tagPtr = tag; *tagPtr; ) {
            char *e              = strchr(tagPtr, ',');
            if (!e) {
              e                  = strchr(tag, '\000');
            } else {
              *e++               = '\000';
            }
            condTrue             = userCSSGetDefine(tagPtr) ||
                                   strstr(userAgent, tagPtr) != NULL;
            if (*e) {
              e[-1]              = ',';
            }
            if (condTrue) {
              break;
            }
            tagPtr               = e;
          }

          // If we find any conditionals, then we need to make a copy of
          // the text document. We do this lazily, as presumably the majority
          // of text documents won't have conditionals.
          if (body == start) {
            check(body           = malloc(end - start));
            memcpy(body, start, end - start);
            bodyEnd             += body - start;
            ptr                 += body - start;
            eol                 += body - start;
          }

          // Remember the beginning of the "[if ...]" statement
          ifPtr                  = ptr;
        }
      } else if (ifPtr && !elsePtr && eol - ptr >= (ssize_t)strlen(tag) + 7 &&
                 !memcmp(ptr, "[else ", 6) &&
                 !memcmp(ptr + 6, tag, strlen(tag)) &&
                 ptr[6 + strlen(tag)] == ']') {
        // Found an "[else ...]" statement. Remember where it started.
        elsePtr                  = ptr;
      } else if (ifPtr && eol - ptr >= (ssize_t)strlen(tag) + 8 &&
                 !memcmp(ptr, "[endif ", 7) &&
                 !memcmp(ptr + 7, tag, strlen(tag)) &&
                 ptr[7 + strlen(tag)] == ']') {
        // Found the closing "[endif ...]" statement. Now we can remove those
        // parts of the conditionals that do not apply to this user agent.
        char *s, *e;
        if (condTrue) {
          s                      = strchr(ifPtr, '\n') + 1;
          e                      = elsePtr ? elsePtr : ptr;
        } else {
          if (elsePtr) {
            s                    = strchr(elsePtr, '\n') + 1;
            e                    = ptr;
          } else {
            s                    = ifPtr;
            e                    = ifPtr;
          }
        }
        memmove(ifPtr, s, e - s);
        memmove(ifPtr + (e - s), eol, bodyEnd - eol);
        bodyEnd                 -= (s - ifPtr) + (eol - e);
        eol                      = ifPtr + (e - s);
        ifPtr                    = NULL;
        elsePtr                  = NULL;
        free(tag);
        tag                      = NULL;
      }
      ptr                        = eol;
    }
    free(tag);
  }

  char *response   = stringPrintf(NULL,
                                  "HTTP/1.1 200 OK\r\n"
                                  "Content-Type: %s\r\n"
                                  "Content-Length: %ld\r\n"
                                  "%s\r\n",
                                  contentType, (long)(bodyEnd - body),
                                  body == start ? "" :
                                  "Cache-Control: no-cache\r\n");
  int len          = strlen(response);
  if (strcmp(httpGetMethod(http), "HEAD")) {
    check(response = realloc(response, len + (bodyEnd - body)));
    memcpy(response + len, body, bodyEnd - body);
    len           += bodyEnd - body;
  }

  // If we expanded conditionals, we had to create a temporary copy. Delete
  // it now.
  if (body != start) {
    free(body);
  }

  httpTransfer(http, response, len);
}

static int shellInABoxHttpHandler(HttpConnection *http, void *arg,
                                  const char *buf, int len) {
  checkGraveyard();
  URL *url                = newURL(http, buf, len);
  const HashMap *headers  = httpGetHeaders(http);
  const char *contentType = getFromHashMap(headers, "content-type");

  // Normalize the path info, present the final path element
  const char *pathInfo    = urlGetPathInfo(url);
  int pathInfoLength = 0;
  pathInfo = strrchr (pathInfo, '/');
  if (pathInfo) {
    ++pathInfo;
  } else {
    pathInfo = "";              // Cheap way to get an empty string
  }
  pathInfoLength = strlen (pathInfo);

  if (!pathInfoLength ||
      (pathInfoLength == 5 && !memcmp(pathInfo, "plain", 5)) ||
      (pathInfoLength == 6 && !memcmp(pathInfo, "secure", 6))) {
    // The root page serves the AJAX application.
    if (contentType &&
        !strncasecmp(contentType, "application/x-www-form-urlencoded", 33)) {
      // XMLHttpRequest carrying data between the AJAX application and the
      // client session.
      return dataHandler(http, arg, buf, len, url);
    }
    UNUSED(rootPageSize);
    char *html            = stringPrintf(NULL, rootPageStart,
                                         enableSSL ? "true" : "false");
    serveStaticFile(http, "text/html", html, strrchr(html, '\000'));
    free(html);
  } else if (pathInfoLength == 8 && !memcmp(pathInfo, "beep.wav", 8)) {
    // Serve the audio sample for the console bell.
    serveStaticFile(http, "audio/x-wav", beepStart, beepStart + beepSize - 1);
  } else if (pathInfoLength == 11 && !memcmp(pathInfo, "enabled.gif", 11)) {
    // Serve the checkmark icon used in the context menu
    serveStaticFile(http, "image/gif", enabledStart,
                    enabledStart + enabledSize - 1);
  } else if (pathInfoLength == 11 && !memcmp(pathInfo, "favicon.ico", 11)) {
    // Serve the favicon
    serveStaticFile(http, "image/x-icon", faviconStart,
                    faviconStart + faviconSize - 1);
  } else if (pathInfoLength == 13 && !memcmp(pathInfo, "keyboard.html", 13)) {
    // Serve the keyboard layout
    serveStaticFile(http, "text/html", keyboardLayoutStart,
                    keyboardLayoutStart + keyboardLayoutSize - 1);
  } else if (pathInfoLength == 12 && !memcmp(pathInfo, "keyboard.png", 12)) {
    // Serve the keyboard icon
    serveStaticFile(http, "image/png", keyboardStart,
                    keyboardStart + keyboardSize - 1);
  } else if (pathInfoLength == 14 && !memcmp(pathInfo, "ShellInABox.js", 14)) {
    // Serve both vt100.js and shell_in_a_box.js in the same transaction.
    // Also, indicate to the client whether the server is SSL enabled.
    char *userCSSString   = getUserCSSString(userCSSList);
    char *stateVars       = stringPrintf(NULL,
                                         "serverSupportsSSL = %s;\n"
                                         "disableSSLMenu    = %s;\n"
                                         "suppressAllAudio  = %s;\n"
                                         "linkifyURLs       = %d;\n"
                                         "userCSSList       = %s;\n\n",
                                         enableSSL      ? "true" : "false",
                                         !enableSSLMenu ? "true" : "false",
                                         noBeep         ? "true" : "false",
                                         linkifyURLs, userCSSString);
    free(userCSSString);
    int stateVarsLength   = strlen(stateVars);
    int contentLength     = stateVarsLength +
                            vt100Size - 1 +
                            shellInABoxSize - 1;
    char *response        = stringPrintf(NULL,
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/javascript; charset=utf-8\r\n"
                             "Content-Length: %d\r\n"
                             "\r\n",
                             contentLength);
    int headerLength      = strlen(response);
    if (strcmp(httpGetMethod(http), "HEAD")) {
      check(response      = realloc(response, headerLength + contentLength));
      memcpy(memcpy(memcpy(
          response + headerLength, stateVars, stateVarsLength)+stateVarsLength,
        vt100Start, vt100Size - 1) + vt100Size - 1,
      shellInABoxStart, shellInABoxSize - 1);
    } else {
      contentLength       = 0;
    }
    free(stateVars);
    httpTransfer(http, response, headerLength + contentLength);
  } else if (pathInfoLength == 10 && !memcmp(pathInfo, "styles.css", 10)) {
    // Serve the style sheet.
    serveStaticFile(http, "text/css; charset=utf-8",
                    cssStyleSheet, strrchr(cssStyleSheet, '\000'));
  } else if (pathInfoLength == 16 && !memcmp(pathInfo, "print-styles.css",16)){
    // Serve the style sheet.
    serveStaticFile(http, "text/css; charset=utf-8",
                    printStylesStart, printStylesStart + printStylesSize - 1);
  } else if (pathInfoLength > 8 && !memcmp(pathInfo, "usercss-", 8)) {
    // Server user style sheets (if any)
    struct UserCSS *css   = userCSSList;
    for (int idx          = atoi(pathInfo + 8);
         idx-- > 0 && css; css = css->next ) {
    }
    if (css) {
      serveStaticFile(http, "text/css; charset=utf-8",
                      css->style, css->style + css->styleLen);
    } else {
      httpSendReply(http, 404, "File not found", NO_MSG);
    }
  } else {
    httpSendReply(http, 404, "File not found", NO_MSG);
  }

  deleteURL(url);
  return HTTP_DONE;
}

static int strtoint(const char *s, int minVal, int maxVal) {
  char *ptr;
  if (!*s) {
    fatal("Missing numeric value.");
  }
  long l = strtol(s, &ptr, 10);
  if (*ptr || l < minVal || l > maxVal) {
    fatal("Range error on numeric value \"%s\".", s);
  }
  return l;
}

static void usage(void) {
  // Drop privileges so that we can tell which uid/gid we would normally
  // run at.
  dropPrivileges();
  uid_t r_uid, e_uid, s_uid;
  uid_t r_gid, e_gid, s_gid;
  check(!getresuid(&r_uid, &e_uid, &s_uid));
  check(!getresgid(&r_gid, &e_gid, &s_gid));
  const char *user  = getUserName(r_uid);
  const char *group = getGroupName(r_gid);

  message("Usage: shellinaboxd [OPTIONS]...\n"
          "Starts an HTTP server that serves terminal emulators to AJAX "
          "enabled browsers.\n"
          "\n"
          "List of command line options:\n"
          "  -b, --background[=PIDFILE]  run in background\n"
          "%s"
          "      --css=FILE              attach contents to CSS style sheet\n"
          "      --cgi[=PORTMIN-PORTMAX] run as CGI\n"
          "  -d, --debug                 enable debug mode\n"
          "  -f, --static-file=URL:FILE  serve static file from URL path\n"
          "  -g, --group=GID             switch to this group (default: %s)\n"
          "  -h, --help                  print this message\n"
          "      --linkify=[none|normal|agressive] default is \"normal\"\n"
          "      --localhost-only        only listen on 127.0.0.1\n"
          "      --no-beep               suppress all audio output\n"
          "  -n, --numeric               do not resolve hostnames\n"
          "      --pidfile=PIDFILE       publish pid of daemon process\n"
          "  -p, --port=PORT             select a port (default: %d)\n"
          "  -s, --service=SERVICE       define one or more services\n"
          "%s"
          "  -q, --quiet                 turn off all messages\n"
          "  -u, --user=UID              switch to this user (default: %s)\n"
          "      --user-css=STYLES       defines user-selectable CSS options\n"
          "  -v, --verbose               enable logging messages\n"
          "      --version               prints version information\n"
          "\n"
          "Debug, quiet, and verbose are mutually exclusive.\n"
          "\n"
          "One or more --service arguments define services that should "
          "be made available\n"
          "through the web interface:\n"
          "  SERVICE := <url-path> ':' APP\n"
          "  APP     := "
#ifdef HAVE_BIN_LOGIN
                        "'LOGIN' | "
#endif
                                   "'SSH' [ : <host> ] | "
                        "USER ':' CWD ':' CMD\n"
          "  USER    := %s<username> ':' <groupname>\n"
          "  CWD     := 'HOME' | <dir>\n"
          "  CMD     := 'SHELL' | <cmdline>\n"
          "\n"
          "<cmdline> supports variable expansion:\n"
          "  ${columns} - number of columns\n"
          "  ${gid}     - gid id\n"
          "  ${group}   - group name\n"
          "  ${home}    - home directory\n"
          "  ${lines}   - number of rows\n"
          "  ${peer}    - name of remote peer\n"
          "  ${uid}     - user id\n"
          "  ${url}     - the URL that serves the terminal session\n"
          "  ${user}    - user name\n"
          "\n"
          "One or more --user-css arguments define optional user-selectable "
          "CSS options.\n"
          "These options show up in the right-click context menu:\n"
          "  STYLES  := GROUP { ';' GROUP }*\n"
          "  GROUP   := OPTION { ',' OPTION }*\n"
          "  OPTION  := <label> ':' [ '-' | '+' ] <css-file>\n"
          "\n"
          "OPTIONs that make up a GROUP are mutually exclusive. But "
          "individual GROUPs are\n"
          "independent of each other.\n",
          !serverSupportsSSL() ? "" :
          "  -c, --cert=CERTDIR          set certificate dir "
          "(default: $PWD)\n"
          "      --cert-fd=FD            set certificate file from fd\n",
          group, PORTNUM,
          !serverSupportsSSL() ? "" :
          "  -t, --disable-ssl           disable transparent SSL support\n"
          "      --disable-ssl-menu      disallow changing transport mode\n",
          user, supportsPAM() ? "'AUTH' | " : "");
  free((char *)user);
  free((char *)group);
}

static void destroyExternalFileHashEntry(void *arg ATTR_UNUSED, char *key,
                                         char *value) {
  UNUSED(arg);
  free(key);
  free(value);
}

static void sigHandler(int signo, siginfo_t *info, void *context) {
  if (exiting++) {
    _exit(1);
  }
  siglongjmp(jmpenv, 1);
}

static void parseArgs(int argc, char * const argv[]) {
  int hasSSL               = serverSupportsSSL();
  if (!hasSSL) {
    enableSSL              = 0;
  }
  int demonize             = 0;
  int cgi                  = 0;
  int verbosity            = MSG_DEFAULT;
  externalFiles            = newHashMap(destroyExternalFileHashEntry, NULL);
  HashMap *serviceTable    = newHashMap(destroyServiceHashEntry, NULL);
  UNUSED(stylesSize);
  check(cssStyleSheet      = strdup(stylesStart));

  for (;;) {
    static const char optstring[] = "+hb::c:df:g:np:s:tqu:v";
    static struct option options[] = {
      { "help",             0, 0, 'h' },
      { "background",       2, 0, 'b' },
      { "cert",             1, 0, 'c' },
      { "cert-fd",          1, 0,  0  },
      { "css",              1, 0,  0  },
      { "cgi",              2, 0,  0  },
      { "debug",            0, 0, 'd' },
      { "static-file",      1, 0, 'f' },
      { "group",            1, 0, 'g' },
      { "linkify",          1, 0,  0  },
      { "localhost-only",   0, 0,  0  },
      { "no-beep",          0, 0,  0  },
      { "numeric",          0, 0, 'n' },
      { "pidfile",          1, 0,  0  },
      { "port",             1, 0, 'p' },
      { "service",          1, 0, 's' },
      { "disable-ssl",      0, 0, 't' },
      { "disable-ssl-menu", 0, 0,  0  },
      { "quiet",            0, 0, 'q' },
      { "user",             1, 0, 'u' },
      { "user-css",         1, 0,  0  },
      { "verbose",          0, 0, 'v' },
      { "version",          0, 0,  0  },
      { 0,                  0, 0,  0  } };
    int idx                = -1;
    int c                  = getopt_long(argc, argv, optstring, options, &idx);
    if (c > 0) {
      for (int i = 0; options[i].name; i++) {
        if (options[i].val == c) {
          idx              = i;
          break;
        }
      }
    } else if (c < 0) {
      break;
    }
    if (idx-- <= 0) {
      // Help (or invalid argument)
      usage();
      if (idx < -1) {
        fatal("Failed to parse command line");
      }
      exit(0);
    } else if (!idx--) {
      // Background
      if (cgi) {
        fatal("CGI and background operations are mutually exclusive");
      }
      demonize            = 1;
      if (optarg && pidfile) {
        fatal("Only one pidfile can be given");
      }
      if (optarg && *optarg) {
        check(pidfile     = strdup(optarg));
      }
    } else if (!idx--) {
      // Certificate
      if (!hasSSL) {
        warn("Ignoring certificate directory, as SSL support is unavailable");
      }
      if (certificateFd >= 0) {
        fatal("Cannot set both a certificate directory and file handle");
      }
      if (certificateDir) {
        fatal("Only one certificate directory can be selected");
      }
      struct stat st;
      if (!optarg || !*optarg || stat(optarg, &st) || !S_ISDIR(st.st_mode)) {
        fatal("\"--cert\" expects a directory name");
      }
      check(certificateDir = strdup(optarg));
    } else if (!idx--) {
      // Certificate file descriptor
      if (!hasSSL) {
        warn("Ignoring certificate directory, as SSL support is unavailable");
      }
      if (certificateDir) {
        fatal("Cannot set both a certificate directory and file handle");
      }
      if (certificateFd >= 0) {
        fatal("Only one certificate file handle can be provided");
      }
      if (!optarg || *optarg < '0' || *optarg > '9') {
        fatal("\"--cert-fd\" expects a valid file handle");
      }
      int tmpFd            = strtoint(optarg, 3, INT_MAX);
      certificateFd        = dup(tmpFd);
      if (certificateFd < 0) {
        fatal("Invalid certificate file handle");
      }
      check(!NOINTR(close(tmpFd)));
    } else if (!idx--) {
      // CSS
      struct stat st;
      if (!optarg || !*optarg || stat(optarg, &st) || !S_ISREG(st.st_mode)) {
        fatal("\"--css\" expects a file name");
      }
      FILE *css            = fopen(optarg, "r");
      if (!css) {
        fatal("Cannot read style sheet \"%s\"", optarg);
      } else {
        check(cssStyleSheet= realloc(cssStyleSheet, strlen(cssStyleSheet) +
                                     st.st_size + 2));
        char *newData      = strrchr(cssStyleSheet, '\000');
        *newData++         = '\n';
        if (fread(newData, st.st_size, 1, css) != 1) {
          fatal("Failed to read style sheet \"%s\"", optarg);
        }
        newData[st.st_size]= '\000';
        fclose(css);
      }
    } else if (!idx--) {
      // CGI
      if (demonize) {
        fatal("CGI and background operations are mutually exclusive");
      }
      if (pidfile) {
        fatal("CGI operation and --pidfile= are mutually exclusive");
      }
      if (port) {
        fatal("Cannot specify a port for CGI operation");
      }
      cgi                  = 1;
      if (optarg && *optarg) {
        char *ptr          = strchr(optarg, '-');
        if (!ptr) {
          fatal("Syntax error in port range specification");
        }
        *ptr               = '\000';
        portMin            = strtoint(optarg, 1, 65535);
        *ptr               = '-';
        portMax            = strtoint(ptr + 1, portMin, 65535);
      }
    } else if (!idx--) {
      // Debug
      if (!logIsDefault() && !logIsDebug()) {
        fatal("--debug is mutually exclusive with --quiet and --verbose.");
      }
      verbosity            = MSG_DEBUG;
      logSetLogLevel(verbosity);
    } else if (!idx--) {
      // Static file
      char *ptr, *path, *file;
      if ((ptr             = strchr(optarg, ':')) == NULL) {
        fatal("Syntax error in static-file definition \"%s\".", optarg);
      }
      check(path           = malloc(ptr - optarg + 1));
      memcpy(path, optarg, ptr - optarg);
      path[ptr - optarg]   = '\000';
      check(file           = strdup(ptr + 1));
      if (getRefFromHashMap(externalFiles, path)) {
        fatal("Duplicate static-file definition for \"%s\".", path);
      }
      addToHashMap(externalFiles, path, file);
    } else if (!idx--) {
      // Group
      if (runAsGroup >= 0) {
        fatal("Duplicate --group option.");
      }
      if (!optarg || !*optarg) {
        fatal("\"--group\" expects a group name.");
      }
      runAsGroup           = parseGroupArg(optarg, NULL);
    } else if (!idx--) {
      // Linkify
      if (!strcmp(optarg, "none")) {
        linkifyURLs        = 0;
      } else if (!strcmp(optarg, "normal")) {
        linkifyURLs        = 1;
      } else if (!strcmp(optarg, "aggressive")) {
        linkifyURLs        = 2;
      } else {
        fatal("Invalid argument for --linkify. Must be "
              "\"none\", \"normal\", or \"aggressive\".");
      }
    } else if (!idx--) {
      // Localhost Only
      localhostOnly        = 1;
    } else if (!idx--) {
      // No Beep
      noBeep               = 1;
    } else if (!idx--) {
      // Numeric
      numericHosts         = 1;
    } else if (!idx--) {
      // Pidfile
      if (cgi) {
        fatal("CGI operation and --pidfile= are mutually exclusive");
      }
      if (!optarg || !*optarg) {
        fatal("Must specify a filename for --pidfile= option");
      }
      if (pidfile) {
        fatal("Only one pidfile can be given");
      }
      check(pidfile        = strdup(optarg));
    } else if (!idx--) {
      // Port
      if (port) {
        fatal("Duplicate --port option");
      }
      if (cgi) {
        fatal("Cannot specifiy a port for CGI operation");
      }
      if (!optarg || *optarg < '0' || *optarg > '9') {
        fatal("\"--port\" expects a port number.");
      }
      port = strtoint(optarg, 1, 65535);
    } else if (!idx--) {
      // Service
      struct Service *service;
      service              = newService(optarg);
      if (getRefFromHashMap(serviceTable, service->path)) {
        fatal("Duplicate service description for \"%s\".", service->path);
      }
      addToHashMap(serviceTable, service->path, (char *)service);
    } else if (!idx--) {
      // Disable SSL
      if (!hasSSL) {
        warn("Ignoring disable-ssl option, as SSL support is unavailable");
      }
      enableSSL            = 0;
    } else if (!idx--) {
      // Disable SSL Menu
      if (!hasSSL) {
        warn("Ignoring disable-ssl-menu option, as SSL support is "
             "unavailable");
      }
      enableSSLMenu        = 0;
    } else if (!idx--) {
      // Quiet
      if (!logIsDefault() && !logIsQuiet()) {
        fatal("--quiet is mutually exclusive with --debug and --verbose.");
      }
      verbosity            = MSG_QUIET;
      logSetLogLevel(verbosity);
    } else if (!idx--) {
      // User
      if (runAsUser >= 0) {
        fatal("Duplicate --user option.");
      }
      if (!optarg || !*optarg) {
        fatal("\"--user\" expects a user name.");
      }
      runAsUser            = parseUserArg(optarg, NULL);
    } else if (!idx--) {
      // User CSS
      if (!optarg || !*optarg) {
        fatal("\"--user-css\" expects a list of styles sheets and labels");
      }
      parseUserCSS(&userCSSList, optarg);
    } else if (!idx--) {
      // Verbose
      if (!logIsDefault() && (!logIsInfo() || logIsDebug())) {
        fatal("--verbose is mutually exclusive with --debug and --quiet");
      }
      verbosity            = MSG_INFO;
      logSetLogLevel(verbosity);
    } else if (!idx--) {
      // Version
      message("ShellInABox version " VERSION " (revision " VCS_REVISION ")");
      exit(0);
    }
  }
  if (optind != argc) {
    usage();
    fatal("Failed to parse command line");
  }
  char *buf                = NULL;
  check(argc >= 1);
  for (int i = 0; i < argc; i++) {
    buf                    = stringPrintf(buf, " %s", argv[i]);
  }
  info("Command line:%s", buf);
  free(buf);

  // If the user did not specify a port, use the default one
  if (!cgi && !port) {
    port                   = PORTNUM;
  }

  // If the user did not register any services, provide the default service
  if (!getHashmapSize(serviceTable)) {
    addToHashMap(serviceTable, "/",
                 (char *)newService(
#ifdef HAVE_BIN_LOGIN
                                    geteuid() ? ":SSH" : ":LOGIN"
#else
                                    ":SSH"
#endif
                                    ));
  }
  enumerateServices(serviceTable);
  deleteHashMap(serviceTable);

  // Do not allow non-root URLs for CGI operation
  if (cgi) {
    for (int i = 0; i < numServices; i++) {
      if (strcmp(services[i]->path, "/")) {
        fatal("Non-root service URLs are incompatible with CGI operation");
      }
    }
    check(cgiSessionKey    = newSessionKey());
  }

  if (demonize) {
    pid_t pid;
    check((pid             = fork()) >= 0);
    if (pid) {
      _exit(0);
    }
    setsid();
  }
  if (pidfile) {
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
    int fd                 = NOINTR(open(pidfile,
                                         O_WRONLY|O_TRUNC|O_LARGEFILE|O_CREAT,
                                         0644));
    if (fd >= 0) {
      char buf[40];
      NOINTR(write(fd, buf, snprintf(buf, 40, "%d", (int)getpid())));
      check(!NOINTR(close(fd)));
    } else {
      free((char *)pidfile);
      pidfile              = NULL;
    }
  }
}

static void removeLimits() {
  static int res[] = { RLIMIT_CPU, RLIMIT_DATA, RLIMIT_FSIZE, RLIMIT_NPROC };
  for (unsigned i = 0; i < sizeof(res)/sizeof(int); i++) {
    struct rlimit rl;
    getrlimit(res[i], &rl);
    if (rl.rlim_max < RLIM_INFINITY) {
      rl.rlim_max  = RLIM_INFINITY;
      setrlimit(res[i], &rl);
      getrlimit(res[i], &rl);
    }
    if (rl.rlim_cur < rl.rlim_max) {
      rl.rlim_cur  = rl.rlim_max;
      setrlimit(res[i], &rl);
    }
  }
}

static void setUpSSL(Server *server) {
  serverEnableSSL(server, enableSSL);

  // Enable SSL support (if available)
  if (enableSSL) {
    check(serverSupportsSSL());
    if (certificateFd >= 0) {
      serverSetCertificateFd(server, certificateFd);
    } else if (certificateDir) {
      char *tmp;
      if (strchr(certificateDir, '%')) {
        fatal("Invalid certificate directory name \"%s\".", certificateDir);
      }
      check(tmp = stringPrintf(NULL, "%s/certificate%%s.pem", certificateDir));
      serverSetCertificate(server, tmp, 1);
      free(tmp);
    } else {
      serverSetCertificate(server, "certificate%s.pem", 1);
    }
  }
}

int main(int argc, char * const argv[]) {
#ifdef HAVE_SYS_PRCTL_H
  // Disable core files
  prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#endif
  struct rlimit rl = { 0 };
  setrlimit(RLIMIT_CORE, &rl);
  removeLimits();

  // Parse command line arguments
  parseArgs(argc, argv);

  // Fork the launcher process, allowing us to drop privileges in the main
  // process.
  int launcherFd  = forkLauncher();

  // Make sure that our timestamps will print in the standard format
  setlocale(LC_TIME, "POSIX");

  // Create a new web server
  Server *server;
  if (port) {
    check(server  = newServer(localhostOnly, port));
    dropPrivileges();
    setUpSSL(server);
  } else {
    // For CGI operation we fork the new server, so that it runs in the
    // background.
    pid_t pid;
    int   fds[2];
    dropPrivileges();
    check(!pipe(fds));
    check((pid    = fork()) >= 0);
    if (pid) {
      // Wait for child to output initial HTML page
      char wait;
      check(!NOINTR(close(fds[1])));
      check(!NOINTR(read(fds[0], &wait, 1)));
      check(!NOINTR(close(fds[0])));
      _exit(0);
    }
    check(!NOINTR(close(fds[0])));
    check(server  = newCGIServer(localhostOnly, portMin, portMax,
                                 AJAX_TIMEOUT));
    cgiServer     = server;
    setUpSSL(server);

    // Output a <frameset> that includes our root page
    check(port    = serverGetListeningPort(server));
    printf("X-ShellInABox-Port: %d\r\n"
           "X-ShellInABox-Pid: %d\r\n"
           "Content-type: text/html; charset=utf-8\r\n\r\n",
           port, getpid());
    UNUSED(cgiRootSize);
    printfUnchecked(cgiRootStart, port, cgiSessionKey);
    fflush(stdout);
    check(!NOINTR(close(fds[1])));
    closeAllFds((int []){ launcherFd, serverGetFd(server) }, 2);
    logSetLogLevel(MSG_QUIET);
  }

  // Set log file format
  serverSetNumericHosts(server, numericHosts ||
                        logIsQuiet() || logIsDefault());

  // Disable /quit handler
  serverRegisterHttpHandler(server, "/quit", NULL, NULL);

  // Register HTTP handler(s)
  for (int i = 0; i < numServices; i++) {
    serverRegisterHttpHandler(server, services[i]->path,
                              shellInABoxHttpHandler, services[i]);
  }

  // Register handlers for external files
  iterateOverHashMap(externalFiles, registerExternalFiles, server);

  // Start the server
  if (!sigsetjmp(jmpenv, 1)) {
    // Clean up upon orderly shut down. Do _not_ cleanup if we die
    // unexpectedly, as we cannot guarantee if we are still in a valid
    // static. This means, we should never catch SIGABRT.
    static const int signals[] = { SIGHUP, SIGINT, SIGQUIT, SIGTERM };
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigHandler;
    sa.sa_flags   = SA_SIGINFO | SA_RESETHAND;
    for (int i = 0; i < sizeof(signals)/sizeof(*signals); ++i) {
      sigaction(signals[i], &sa, NULL);
    }
    serverLoop(server);
  }

  // Clean up
  deleteServer(server);
  finishAllSessions();
  deleteHashMap(externalFiles);
  for (int i = 0; i < numServices; i++) {
    deleteService(services[i]);
  }
  free(services);
  free(certificateDir);
  free(cgiSessionKey);
  if (pidfile) {
    // As a convenience, remove the pidfile, if it is still the version that
    // we wrote. In general, pidfiles are not expected to be incredibly
    // reliable, as there is no way to properly deal with multiple programs
    // accessing the same pidfile. But we at least make a best effort to be
    // good citizens.
    char buf[40];
    int fd        = open(pidfile, O_RDONLY);
    if (fd >= 0) {
      ssize_t sz;
      NOINTR(sz   = read(fd, buf, sizeof(buf)-1));
      NOINTR(close(fd));
      if (sz > 0) {
        buf[sz]   = '\000';
        if (atoi(buf) == getpid()) {
          unlink(pidfile);
        }
      }
    }
    free((char *)pidfile);
  }
  info("Done");
  _exit(0);
}
