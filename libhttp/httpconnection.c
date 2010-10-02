// httpconnection.c -- Manage state machine for HTTP connections
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

#include <errno.h>
#include <arpa/inet.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#ifdef HAVE_STRLCAT
#define strncat(a,b,c) ({ char *_a = (a); strlcat(_a, (b), (c)+1); _a; })
#endif
#ifndef HAVE_ISNAN
#define isnan(x) ({ typeof(x) _x = (x); _x != _x; })
#endif
#define max(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b);                  \
                     _a > _b ? _a : _b; })
#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

#include "libhttp/httpconnection.h"
#include "logging/logging.h"

#define MAX_HEADER_LENGTH   (64<<10)
#define CONNECTION_TIMEOUT  (10*60)

static int httpPromoteToSSL(struct HttpConnection *http, const char *buf,
                            int len) {
  if (http->ssl->enabled && !http->sslHndl) {
    debug("Switching to SSL (replaying %d+%d bytes)",
          http->partialLength, len);
    if (http->partial && len > 0) {
      check(http->partial  = realloc(http->partial,
                                     http->partialLength + len));
      memcpy(http->partial + http->partialLength, buf, len);
      http->partialLength += len;
    }
    int rc                 = sslPromoteToSSL(
                                    http->ssl, &http->sslHndl, http->fd,
                                    http->partial ? http->partial : buf,
                                    http->partial ? http->partialLength : len);
    if (http->sslHndl) {
      check(!rc);
      SSL_set_app_data(http->sslHndl, http);
    }
    free(http->partial);
    http->partialLength    = 0;
    return rc;
  } else {
    errno                  = EINVAL;
    return -1;
  }
}

static ssize_t httpRead(struct HttpConnection *http, char *buf, ssize_t len) {
  sslBlockSigPipe();
  int rc;
  if (http->sslHndl) {
    dcheck(!ERR_peek_error());
    rc                        = SSL_read(http->sslHndl, buf, len);
    switch (rc) {
    case 0:
    case -1:
      switch (http->lastError = SSL_get_error(http->sslHndl, rc)) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        errno                 = EAGAIN;
        rc                    = -1;
        break;
      default:
        errno                 = EINVAL;
        break;
      }
      ERR_clear_error();
      break;
    default:
      break;
    }
    dcheck(!ERR_peek_error());
  } else {
    rc = NOINTR(read(http->fd, buf, len));
  }
  sslUnblockSigPipe();
  if (rc > 0) {
    serverSetTimeout(httpGetServerConnection(http), CONNECTION_TIMEOUT);
  }
  return rc;
}

static ssize_t httpWrite(struct HttpConnection *http, const char *buf,
                         ssize_t len) {
  sslBlockSigPipe();
  int rc;
  if (http->sslHndl) {
    dcheck(!ERR_peek_error());
    rc                        = SSL_write(http->sslHndl, buf, len);
    switch (rc) {
    case 0:
    case -1:
      switch (http->lastError = SSL_get_error(http->sslHndl, rc)) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        errno                 = EAGAIN;
        rc                    = -1;
        break;
      default:
        errno                 = EINVAL;
        break;
      }
      ERR_clear_error();
      break;
    default:
      break;
    }
    dcheck(!ERR_peek_error());
  } else {
    rc = NOINTR(write(http->fd, buf, len));
  }
  sslUnblockSigPipe();
  return rc;
}

static int httpShutdown(struct HttpConnection *http, int how) {
  if (http->sslHndl) {
    int rc        = 0;
    if (how != SHUT_RD) {
      dcheck(!ERR_peek_error());
      for (int i = 0; i < 10; i++) {
        sslBlockSigPipe();
        rc        = SSL_shutdown(http->sslHndl);
        int sPipe = sslUnblockSigPipe();
        if (rc > 0) {
          rc      = 0;
          break;
        } else {
          rc      = -1;
          // Retry a few times in order to prefer a clean bidirectional
          // shutdown. But don't bother if the other side already closed
          // the connection.
          if (sPipe) {
            break;
          }
        }
      }
      sslFreeHndl(&http->sslHndl);
    }
  }
  return shutdown(http->fd, how);
}

static void httpCloseRead(struct HttpConnection *http) {
  if (!http->closed) {
    httpShutdown(http, SHUT_RD);
    http->closed = 1;
  }
}

#ifndef HAVE_STRCASESTR
static char *strcasestr(const char *haystack, const char *needle) {
  // This algorithm is O(len(haystack)*len(needle)). Much better algorithms
  // are available, but this code is much simpler and performance is not
  // critical for our workloads.
  int len = strlen(needle);
  do {
    if (!strncasecmp(haystack, needle, len)) {
      return haystack;
    }
  } while (*haystack++);
  return NULL;
}
#endif

static int httpFinishCommand(struct HttpConnection *http) {
  int rc            = HTTP_DONE;
  if ((http->callback || http->websocketHandler) && !http->done) {
    rc              = http->callback ? http->callback(http, http->arg, NULL, 0)
       : http->websocketHandler(http, http->arg, WS_CONNECTION_CLOSED, NULL,0);
    check(rc != HTTP_SUSPEND);
    check(rc != HTTP_PARTIAL_REPLY);
    http->callback  = NULL;
    http->arg       = NULL;
    if (rc == HTTP_ERROR) {
      httpCloseRead(http);
    }
  }
  if (!http->closed) {
    const char *con = getFromHashMap(&http->header, "connection");
    if ((con && strcasestr(con, "close")) ||
        !http->version || strcmp(http->version, "HTTP/1.1") < 0) {
      httpCloseRead(http);
    }
  }
  if (logIsInfo()) {
    check(http->method);
    check(http->path);
    check(http->version);
    if (http->peerName) {
      time_t t      = currentTime;
      struct tm *ltime;
      check (ltime  = localtime(&t));
      char timeBuf[80];
      char lengthBuf[40];
      check(strftime(timeBuf, sizeof(timeBuf),
                     "[%d/%b/%Y:%H:%M:%S %z]", ltime));
      if (http->totalWritten > 0) {
        snprintf(lengthBuf, sizeof(lengthBuf), "%d", http->totalWritten);
      } else {
        *lengthBuf  = '\000';
        strncat(lengthBuf, "-", sizeof(lengthBuf)-1);
      }
      info("%s - - %s \"%s %s %s\" %d %s",
           http->peerName, timeBuf, http->method, http->path, http->version,
           http->code, lengthBuf);
    }
  }
  return rc;
}

static void httpDestroyHeaders(void *arg ATTR_UNUSED, char *key, char *value) {
  UNUSED(arg);
  free(key);
  free(value);
}

static char *getPeerName(int fd, int *port, int numericHosts) {
  struct sockaddr peerAddr;
  socklen_t sockLen = sizeof(peerAddr);
  if (getpeername(fd, &peerAddr, &sockLen)) {
    if (port) {
      *port         = -1;
    }
    return NULL;
  }
  char host[256];
  if (numericHosts ||
      getnameinfo(&peerAddr, sockLen, host, sizeof(host), NULL, 0, NI_NOFQDN)){
    check(inet_ntop(peerAddr.sa_family,
                    &((struct sockaddr_in *)&peerAddr)->sin_addr,
                    host, sizeof(host)));
  }
  if (port) {
    *port           = ntohs(((struct sockaddr_in *)&peerAddr)->sin_port);
  }
  char *ret;
  check(ret         = strdup(host));
  return ret;
}

static void httpSetState(struct HttpConnection *http, int state) {
  if (state == (int)http->state) {
    return;
  }

  if (state == COMMAND) {
    if (http->state != SNIFFING_SSL) {
      int rc                 = httpFinishCommand(http);
      check(rc != HTTP_SUSPEND);
      check(rc != HTTP_PARTIAL_REPLY);
    }
    check(!http->private);
    free(http->url);
    free(http->method);
    free(http->path);
    free(http->matchedPath);
    free(http->pathInfo);
    free(http->query);
    free(http->version);
    http->done               = 0;
    http->url                = NULL;
    http->method             = NULL;
    http->path               = NULL;
    http->matchedPath        = NULL;
    http->pathInfo           = NULL;
    http->query              = NULL;
    http->version            = NULL;
    destroyHashMap(&http->header);
    initHashMap(&http->header, httpDestroyHeaders, NULL);
    http->headerLength       = 0;
    http->callback           = NULL;
    http->arg                = NULL;
    http->totalWritten       = 0;
    http->code               = 200;
  }
  http->state                = state;
}

struct HttpConnection *newHttpConnection(struct Server *server, int fd,
                                         int port, struct SSLSupport *ssl,
                                         int numericHosts) {
  struct HttpConnection *http;
  check(http = malloc(sizeof(struct HttpConnection)));
  initHttpConnection(http, server, fd, port, ssl, numericHosts);
  return http;
}

void initHttpConnection(struct HttpConnection *http, struct Server *server,
                        int fd, int port, struct SSLSupport *ssl,
                        int numericHosts) {
  http->server             = server;
  http->connection         = NULL;
  http->fd                 = fd;
  http->port               = port;
  http->closed             = 0;
  http->isSuspended        = 0;
  http->isPartialReply     = 0;
  http->done               = 0;
  http->state              = ssl ? SNIFFING_SSL : COMMAND;
  http->peerName           = getPeerName(fd, &http->peerPort, numericHosts);
  http->url                = NULL;
  http->method             = NULL;
  http->path               = NULL;
  http->matchedPath        = NULL;
  http->pathInfo           = NULL;
  http->query              = NULL;
  http->version            = NULL;
  initHashMap(&http->header, httpDestroyHeaders, NULL);
  http->headerLength       = 0;
  http->key                = NULL;
  http->partial            = NULL;
  http->partialLength      = 0;
  http->msg                = NULL;
  http->msgLength          = 0;
  http->msgOffset          = 0;
  http->totalWritten       = 0;
  http->expecting          = 0;
  http->websocketType      = WS_UNDEFINED;
  http->callback           = NULL;
  http->websocketHandler   = NULL;
  http->arg                = NULL;
  http->private            = NULL;
  http->code               = 200;
  http->ssl                = ssl;
  http->sslHndl            = NULL;
  http->lastError          = 0;
  if (logIsInfo()) {
    debug("Accepted connection from %s:%d",
          http->peerName ? http->peerName : "???", http->peerPort);
  }
}

void destroyHttpConnection(struct HttpConnection *http) {
  if (http) {
    if (http->isSuspended || http->isPartialReply) {
      if (!http->done) {
        if (http->callback) {
          http->callback(http, http->arg, NULL, 0);
        } else if (http->websocketHandler) {
          http->websocketHandler(http, http->arg, WS_CONNECTION_CLOSED,NULL,0);
        }
      }
      http->callback       = NULL;
      http->isSuspended    = 0;
      http->isPartialReply = 0;
    }
    httpSetState(http, COMMAND);
    if (logIsInfo()) {
      debug("Closing connection to %s:%d",
            http->peerName ? http->peerName : "???", http->peerPort);
    }
    httpShutdown(http, http->closed ? SHUT_WR : SHUT_RDWR);
    dcheck(!close(http->fd));
    free(http->peerName);
    free(http->url);
    free(http->method);
    free(http->path);
    free(http->matchedPath);
    free(http->pathInfo);
    free(http->query);
    free(http->version);
    destroyHashMap(&http->header);
    free(http->partial);
    free(http->msg);
  }
}

void deleteHttpConnection(struct HttpConnection *http) {
  destroyHttpConnection(http);
  free(http);
}

#ifdef HAVE_ZLIB
static int httpAcceptsEncoding(struct HttpConnection *http,
                               const char *encoding) {
  int encodingLength  = strlen(encoding);
  const char *accepts = getFromHashMap(&http->header, "accept-encoding");
  if (!accepts) {
    return 0;
  }
  double all          = -1.0;
  double match        = -1.0;
  while (*accepts) {
    while (*accepts == ' ' || *accepts == '\t' ||
           *accepts == '\r' || *accepts == '\n') {
      accepts++;
    }
    const char *ptr   = accepts;
    while (*ptr && *ptr != ',' && *ptr != ';' &&
           *ptr != ' ' && *ptr != '\t' &&
           *ptr != '\r' && *ptr != '\n') {
      ptr++;
    }
    int isAll         = ptr - accepts == 1 && *accepts == '*';
    int isMatch       = ptr - accepts == encodingLength &&
                        !strncasecmp(accepts, encoding, encodingLength);
    while (*ptr && *ptr != ';' && *ptr != ',') {
      ptr++;
    }
    double val        = 1.0;
    if (*ptr == ';') {
      ptr++;
      while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') {
        ptr++;
      }
      if ((*ptr | 0x20) == 'q') {
        ptr++;
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') {
          ptr++;
        }
        if (*ptr == '=') {
          val         = strtod(ptr + 1, (char **)&ptr);
        }
      }
    }
    if (isnan(val) || val == -HUGE_VAL || val < 0) {
      val             = 0;
    } else if (val == HUGE_VAL || val > 1.0) {
      val             = 1.0;
    }
    if (isAll) {
      all             = val;
    } else if (isMatch) {
      match           = val;
    }
    while (*ptr && *ptr != ',') {
      ptr++;
    }
    while (*ptr == ',') {
      ptr++;
    }
    accepts           = ptr;
  }
  if (match >= 0.0) {
    return match > 0.0;
  } else {
    return all > 0.0;
  }
}
#endif

static void removeHeader(char *header, int *headerLength, const char *id) {
  check(header);
  check(headerLength);
  check(*headerLength >= 0);
  check(id);
  check(strchr(id, ':'));
  int idLength       = strlen(id);
  if (idLength <= 0) {
    return;
  }
  for (char *ptr = header; header + *headerLength - ptr >= idLength; ) {
    char *end        = ptr;
    do {
      end            = memchr(end, '\n', header + *headerLength - end);
      if (end == NULL) {
        end          = header + *headerLength;
      } else {
        ++end;
      }
    } while (end < header + *headerLength && *end == ' ');
    if (!strncasecmp(ptr, id, idLength)) {
      memmove(ptr, end, header + *headerLength - end);
      *headerLength -= end - ptr;
    } else {
      ptr            = end;
    }
  }
}

static void addHeader(char **header, int *headerLength, const char *fmt, ...) {
  check(header);
  check(headerLength);
  check(*headerLength >= 0);
  check(strstr(fmt, "\r\n"));

  va_list ap;
  va_start(ap, fmt);
  char *tmp        = vStringPrintf(NULL, fmt, ap);
  va_end(ap);
  int tmpLength    = strlen(tmp);

  if (*headerLength >= 2 && !memcmp(*header + *headerLength - 2, "\r\n", 2)) {
    *headerLength -= 2;
  }
  check(*header    = realloc(*header, *headerLength + tmpLength + 2));

  memcpy(*header + *headerLength, tmp, tmpLength);
  memcpy(*header + *headerLength + tmpLength, "\r\n", 2);
  *headerLength   += tmpLength + 2;
  free(tmp);
}

void httpTransfer(struct HttpConnection *http, char *msg, int len) {
  check(msg);
  check(len >= 0);

  // Internet Explorer seems to have difficulties with compressed data. It
  // also has difficulties with SSL connections that are being proxied.
  int ieBug                 = 0;
  const char *userAgent     = getFromHashMap(&http->header, "user-agent");
  const char *msie          = userAgent ? strstr(userAgent, "MSIE ") : NULL;
  if (msie) {
    ieBug++;
  }

  char *header              = NULL;
  int headerLength          = 0;
  int bodyOffset            = 0;

  int compress              = 0;
  if (!http->totalWritten) {
    // Perform some basic sanity checks. This does not necessarily catch all
    // possible problems, though.
    int l                   = len;
    char *line              = msg;
    for (char *eol, *lastLine = NULL;
         l > 0 && (eol = memchr(line, '\n', l)) != NULL; ) {
      // All lines end in CR LF
      check(eol[-1] == '\r');
      if (!lastLine) {
        // The first line looks like "HTTP/1.x STATUS\r\n"
        check(eol - line > 11);
        check(!memcmp(line, "HTTP/1.", 7));
        check(line[7] >= '0' && line[7] <= '9' &&
              (line[8] == ' ' || line[8] == '\t'));
        int i               = eol - line - 9;
        for (char *ptr = line + 9; i-- > 0; ) {
          char ch           = *ptr++;
          if (ch < '0' || ch > '9') {
            check(ptr > line + 10);
            check(ch == ' ' || ch == '\t');
            break;
          }
        }
        check(i > 1);
      } else if (line + 1 == eol) {
        // Found the end of the headers.

        // Check that we don't send any data with HEAD requests
        int isHead          = !strcmp(http->method, "HEAD");
        check(l == 2 || !isHead);

        #ifdef HAVE_ZLIB
        // Compress replies that might exceed the size of a single IP packet
        compress            = !ieBug && !isHead &&
                              !http->isPartialReply &&
                              len > 1400 &&
                              httpAcceptsEncoding(http, "deflate");
        #endif
        break;
      } else {
        // Header lines either contain a colon, or they are continuation
        // lines
        if (*line != ' ' && *line != '\t') {
          check(memchr(line, ':', eol - line));
        }
      }
      lastLine              = line;
      l                    -= eol - line + 1;
      line                  = eol + 1;
    }

    if (ieBug || compress) {
      if (l >= 2 && !memcmp(line, "\r\n", 2)) {
        line               += 2;
        l                  -= 2;
      }
      headerLength          = line - msg;
      bodyOffset            = headerLength;
      check(header          = malloc(headerLength));
      memcpy(header, msg, headerLength);
    }
    if (ieBug) {
      removeHeader(header, &headerLength, "connection:");
      addHeader(&header, &headerLength, "Connection: close\r\n");
    }

    if (compress) {
      #ifdef HAVE_ZLIB
      // Compress the message
      char *compressed;
      check(compressed      = malloc(len));
      check(len >= bodyOffset + 2);
      z_stream strm         = { .zalloc    = Z_NULL,
                                .zfree     = Z_NULL,
                                .opaque    = Z_NULL,
                                .avail_in  = l,
                                .next_in   = (unsigned char *)line,
                                .avail_out = len,
                                .next_out  = (unsigned char *)compressed
                              };
      if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) == Z_OK) {
        if (deflate(&strm, Z_FINISH) == Z_STREAM_END) {
          // Compression was successful and resulted in reduction in size
          debug("Compressed response from %d to %d", len, len-strm.avail_out);
          free(msg);
          msg               = compressed;
          len              -= strm.avail_out;
          bodyOffset        = 0;
          removeHeader(header, &headerLength, "content-length:");
          removeHeader(header, &headerLength, "content-encoding:");
          addHeader(&header, &headerLength, "Content-Length: %d\r\n", len);
          addHeader(&header, &headerLength, "Content-Encoding: deflate\r\n");
        } else {
          free(compressed);
        }
        deflateEnd(&strm);
      } else {
        free(compressed);
      }
      #endif
    }
  }

  http->totalWritten       += headerLength + (len - bodyOffset);
  if (!headerLength) {
    free(header);
  } else if (http->msg) {
    check(http->msg         = realloc(http->msg,
                                      http->msgLength - http->msgOffset +
                                      max(http->msgOffset, headerLength)));
    if (http->msgOffset) {
      memmove(http->msg, http->msg + http->msgOffset,
              http->msgLength - http->msgOffset);
      http->msgLength      -= http->msgOffset;
      http->msgOffset       = 0;
    }
    memcpy(http->msg + http->msgLength, header, headerLength);
    http->msgLength        += headerLength;
    free(header);
  } else {
    check(!http->msgOffset);
    http->msg               = header;
    http->msgLength         = headerLength;
  }

  if (len <= bodyOffset) {
    free(msg);
  } else if (http->msg) {
    check(http->msg         = realloc(http->msg,
                                      http->msgLength - http->msgOffset +
                                      max(http->msgOffset, len - bodyOffset)));
    if (http->msgOffset) {
      memmove(http->msg, http->msg + http->msgOffset,
              http->msgLength - http->msgOffset);
      http->msgLength      -= http->msgOffset;
      http->msgOffset       = 0;
    }
    memcpy(http->msg + http->msgLength, msg + bodyOffset, len - bodyOffset);
    http->msgLength        += len - bodyOffset;
    free(msg);
  } else {
    check(!http->msgOffset);
    if (bodyOffset) {
      memmove(msg, msg + bodyOffset, len - bodyOffset);
    }
    http->msg               = msg;
    http->msgLength         = len - bodyOffset;
  }

  // The caller can suspend the connection, so that it can send an
  // asynchronous reply. Once the reply has been sent, the connection
  // gets reactivated. Normally, this means it would go back to listening
  // for commands.
  // Similarly, the caller can indicate that this is a partial message and
  // return additional data in subsequent calls to the callback handler.
  if (http->isSuspended || http->isPartialReply) {
    if (http->msg && http->msgLength > 0) {
      int wrote             = httpWrite(http, http->msg, http->msgLength);
      if (wrote < 0 && errno != EAGAIN) {
        httpCloseRead(http);
        free(http->msg);
        http->msgLength     = 0;
        http->msg           = NULL;
      } else if (wrote > 0) {
        if (wrote == http->msgLength) {
          free(http->msg);
          http->msgLength   = 0;
          http->msg         = NULL;
        } else {
          memmove(http->msg, http->msg + wrote, http->msgLength - wrote);
          http->msgLength  -= wrote;
        }
      }
    }

    check(http->state == PAYLOAD || http->state == DISCARD_PAYLOAD);
    if (!http->isPartialReply) {
      if (http->expecting < 0) {
        // If we do not know the length of the content, close the connection.
        debug("Closing previously suspended connection");
        httpCloseRead(http);
        httpSetState(http, DISCARD_PAYLOAD);
      } else if (http->expecting == 0) {
        httpSetState(http, COMMAND);
        http->isSuspended  = 0;
        struct ServerConnection *connection = httpGetServerConnection(http);
        if (!serverGetTimeout(connection)) {
          serverSetTimeout(connection, CONNECTION_TIMEOUT);
        }
        serverConnectionSetEvents(http->server, connection, http->fd,
                                  http->msgLength ? POLLIN|POLLOUT : POLLIN);
      }
    }
  }

  if (ieBug) {
    httpCloseRead(http);
  }
}

void httpTransferPartialReply(struct HttpConnection *http, char *msg, int len){
  check(!http->isSuspended);
  http->isPartialReply = 1;
  if (http->state != PAYLOAD && http->state != DISCARD_PAYLOAD) {
    check(http->state == HEADERS);
    httpSetState(http, PAYLOAD);
  }
  httpTransfer(http, msg, len);
}

static int httpHandleCommand(struct HttpConnection *http,
                             const struct Trie *handlers) {
  debug("Handling \"%s\" \"%s\"", http->method, http->path);
  const char *contentLength                  = getFromHashMap(&http->header,
                                                             "content-length");
  if (contentLength != NULL && *contentLength) {
    char *endptr;
    http->expecting                          = strtol(contentLength,
                                                      &endptr, 10);
    if (*endptr) {
      // Invalid length. Read until end of stream and then close
      // connection.
      http->expecting                        = -1;
    }
  } else {
      // Unknown length. Read until end of stream and then close
      // connection.
    http->expecting                          = -1;
  }
  if (!strcmp(http->method, "OPTIONS")) {
    char *response                           = stringPrintf(NULL,
                                                "HTTP/1.1 200 OK\r\n"
                                                "Content-Length: 0\r\n"
                                                "Allow: GET, POST, OPTIONS\r\n"
                                                "\r\n");
    httpTransfer(http, response, strlen(response));
    if (http->expecting < 0) {
      http->expecting                        = 0;
    }
    return HTTP_READ_MORE;
  } else if (!strcmp(http->method, "GET")) {
    if (http->expecting < 0) {
      http->expecting                        = 0;
    }
  } else if (!strcmp(http->method, "POST")) {
  } else if (!strcmp(http->method, "HEAD")) {
    if (http->expecting < 0) {
      http->expecting                        = 0;
    }
  } else if (!strcmp(http->method, "PUT")    ||
             !strcmp(http->method, "DELETE") ||
             !strcmp(http->method, "TRACE")  ||
             !strcmp(http->method, "CONNECT")) {
    httpSendReply(http, 405, "Method Not Allowed", NO_MSG);
    return HTTP_DONE;
  } else {
    httpSendReply(http, 501, "Method Not Implemented", NO_MSG);
    return HTTP_DONE;
  }
  const char *host                           = getFromHashMap(&http->header,
                                                              "host");
  if (host) {
    for (char ch, *ptr = (char *)host; (ch = *ptr) != '\000'; ptr++) {
      if (ch == ':') {
        *ptr                                 = '\000';
        break;
      }
      if (ch != '-' && ch != '.' &&
          (ch < '0' ||(ch > '9' && ch < 'A') ||
          (ch > 'Z' && ch < 'a')||(ch > 'z' && ch <= 0x7E))) {
        httpSendReply(http, 400, "Bad Request", NO_MSG);
        return HTTP_DONE;
      }
    }
  }

  char *diff;
  struct HttpHandler *h = (struct HttpHandler *)getFromTrie(handlers,
                                                            http->path, &diff);

  if (h) {
    if (h->websocketHandler) {
      // Check for WebSocket handshake
      const char *upgrade                    = getFromHashMap(&http->header,
                                                              "upgrade");
      if (upgrade && !strcmp(upgrade, "WebSocket")) {
        const char *connection               = getFromHashMap(&http->header,
                                                              "connection");
        if (connection && !strcmp(connection, "Upgrade")) {
          const char *origin                 = getFromHashMap(&http->header,
                                                              "origin");
          if (origin) {
            for (const char *ptr = origin; *ptr; ptr++) {
              if ((unsigned char)*ptr < ' ') {
                goto bad_ws_upgrade;
              }
            }

            const char *protocol             = getFromHashMap(&http->header,
                                                         "websocket-protocol");
            if (protocol) {
              for (const char *ptr = protocol; *ptr; ptr++) {
                if ((unsigned char)*ptr < ' ') {
                  goto bad_ws_upgrade;
                }
              }
            }
            char *port                       = NULL;
            if (http->port != (http->sslHndl ? 443 : 80)) {
              port                           = stringPrintf(NULL,
                                                            ":%d", http->port);
            }
            char *response                   = stringPrintf(NULL,
              "HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
              "Upgrade: WebSocket\r\n"
              "Connection: Upgrade\r\n"
              "WebSocket-Origin: %s\r\n"
              "WebSocket-Location: %s://%s%s%s\r\n"
              "%s%s%s"
              "\r\n",
              origin,
              http->sslHndl ? "wss" : "ws", host && *host ? host : "localhost",
              port ? port : "", http->path,
              protocol ? "WebSocket-Protocol: " : "",
              protocol ? protocol : "",
              protocol ? "\r\n" : "");
            free(port);
            debug("Switching to WebSockets");
            httpTransfer(http, response, strlen(response));
            if (http->expecting < 0) {
              http->expecting                = 0;
            }
            http->websocketHandler           = h->websocketHandler;
            httpSetState(http, WEBSOCKET);
            return HTTP_READ_MORE;
          }
        }
      }
    }
  bad_ws_upgrade:;

    if (h->handler) {
      check(diff);
      while (diff > http->path && diff[-1] == '/') {
        diff--;
      }
      if (!*diff || *diff == '/' || *diff == '?' || *diff == '#') {
        check(!http->matchedPath);
        check(!http->pathInfo);
        check(!http->query);

        check(http->matchedPath              = malloc(diff - http->path + 1));
        memcpy(http->matchedPath, http->path, diff - http->path);
        http->matchedPath[diff - http->path] = '\000';

        const char *query = strchr(diff, '?');
        if (*diff && *diff != '?') {
          const char *endOfInfo              = query
                                               ? query : strrchr(diff, '\000');
          check(http->pathInfo               = malloc(endOfInfo - diff + 1));
          memcpy(http->pathInfo, diff, endOfInfo - diff);
          http->pathInfo[endOfInfo - diff]   = '\000';
        }

        if (query) {
          check(http->query                  = strdup(query + 1));
        }
        return h->handler(http, h->arg);
      }
    }
  }
  httpSendReply(http, 404, "File Not Found", NO_MSG);
  return HTTP_DONE;
}

static int httpGetChar(struct HttpConnection *http, const char *buf,
                       int size, int *offset) {
  if (*offset < 0) {
    return (unsigned char)http->partial[http->partialLength + (*offset)++];
  } else if (*offset < size) {
    return (unsigned char)buf[(*offset)++];
  } else {
    return -1;
  }
}

static int httpParseCommand(struct HttpConnection *http, int offset,
                            const char *buf, int bytes, int firstSpace,
                            int lastSpace, int lineLength) {
  if (firstSpace < 1 || lastSpace < 0) {
  bad_request:
    if (!http->method) {
      check(http->method  = strdup(""));
    }
    if (!http->path) {
      check(http->path    = strdup(""));
    }
    if (!http->version) {
      check(http->version = strdup(""));
    }
    httpSendReply(http, 400, "Bad Request", NO_MSG);
    httpSetState(http, COMMAND);
    return 0;
  }
  check(!http->method);
  check(http->method      = malloc(firstSpace + 1));
  int i                   = offset;
  int j                   = 0;
  for (; j < firstSpace; j++) {
    int ch                = httpGetChar(http, buf, bytes, &i);
    if (ch >= 'a' && ch <= 'z') {
      ch                 &= ~0x20;
    }
    http->method[j]       = ch;
  }
  http->method[j]         = '\000';
  check(!http->path);
  check(http->path        = malloc(lastSpace - firstSpace));
  j                       = 0;
  while (i < offset + lastSpace) {
    int ch                = httpGetChar(http, buf, bytes, &i);
    if ((ch != ' ' && ch != '\t') || j) {
      http->path[j++]     = ch;
    }
  }
  http->path[j]           = '\000';
  if (*http->path != '/' &&
      (strcmp(http->method, "OPTIONS") || strcmp(http->path, "*"))) {
    goto bad_request;
  }
  check(!http->version);
  check(http->version     = malloc(lineLength - lastSpace + 1));
  j                       = 0;
  while (i < offset + lineLength) {
    int ch                = httpGetChar(http, buf, bytes, &i);
    if (ch == '\r') {
      break;
    }
    if (ch >= 'a' && ch <= 'z') {
      ch                 &= ~0x20;
    }
    if ((ch != ' ' && ch != '\t') || j) {
      http->version[j]    = ch;
      j++;
    }
  }
  http->version[j]        = '\000';
  if (memcmp(http->version, "HTTP/", 5) ||
      (http->version[5] < '1' || http->version[5] > '9')) {
    goto bad_request;
  }
  httpSetState(http, HEADERS);
  return 1;
}

static int httpParseHeaders(struct HttpConnection *http,
                            const struct Trie *handlers, int offset,
                            const char *buf, int bytes, int colon,
                            int lineLength) {
  int i                    = offset;
  int ch                   = httpGetChar(http, buf, bytes, &i);
  if (ch == ' ' || ch == '\t') {
    if (http->key) {
      char **oldValue      = getRefFromHashMap(&http->header, http->key);
      check(oldValue);
      int oldLength        = strlen(*oldValue);
      check(*oldValue      = realloc(*oldValue,
                                    oldLength + lineLength + 1));
      int j                = oldLength;
      int end              = oldLength + lineLength;
      (*oldValue)[j++]     = ' ';
      for (; j < end; j++) {
        ch                 = httpGetChar(http, buf, bytes, &i);
        if (ch == ' ' || ch == '\t') {
          end--;
          j--;
          continue;
        } else if (ch == '\r' && j == end - 1) {
          break;
        }
        (*oldValue)[j]     = ch;
      }
      (*oldValue)[j]       = '\000';
    }
  } else if ((ch == '\r' &&
              httpGetChar(http, buf, bytes, &i) == '\n') ||
             ch == '\n' || ch == -1) {
    check(!http->expecting);
    http->callback         = NULL;
    http->arg              = NULL;
    int rc                 = httpHandleCommand(http, handlers);
  retry:;
    struct ServerConnection *connection = httpGetServerConnection(http);
    switch (rc) {
    case HTTP_DONE:
    case HTTP_ERROR: {
      if (http->expecting < 0 || rc == HTTP_ERROR) {
        httpCloseRead(http);
      }
      http->done           = 1;
      http->isSuspended    = 0;
      http->isPartialReply = 0;
      if (!serverGetTimeout(connection)) {
        serverSetTimeout(connection, CONNECTION_TIMEOUT);
      }
      httpSetState(http, http->expecting ? DISCARD_PAYLOAD : COMMAND);
      break; }
    case HTTP_READ_MORE:
      http->isSuspended    = 0;
      http->isPartialReply = 0;
      if (!serverGetTimeout(connection)) {
        serverSetTimeout(connection, CONNECTION_TIMEOUT);
      }
      check(!http->done);
      if (!http->expecting) {
        if (http->callback) {
          rc                 = http->callback(http, http->arg, "", 0);
          if (rc != HTTP_READ_MORE) {
            goto retry;
          }
        } else if (http->websocketHandler) {
          http->websocketHandler(http, http->arg, WS_CONNECTION_OPENED,
                                 NULL, 0);
        }
      }
      if (http->state != WEBSOCKET) {
        httpSetState(http, http->expecting ? PAYLOAD : COMMAND);
      }
      break;
    case HTTP_SUSPEND:
      http->isSuspended    = 1;
      http->isPartialReply = 0;
      serverSetTimeout(connection, 0);
      if (http->state != PAYLOAD && http->state != DISCARD_PAYLOAD) {
        check(http->state == HEADERS);
        httpSetState(http, PAYLOAD);
      }
      break;
    case HTTP_PARTIAL_REPLY:
      http->isSuspended    = 0;
      http->isPartialReply = 1;
      if (http->state != PAYLOAD && http->state != DISCARD_PAYLOAD) {
        check(http->state == HEADERS);
        httpSetState(http, PAYLOAD);
      }
      break;
    default:
      check(0);
    }
    if (ch == -1) {
      httpCloseRead(http);
    }
  } else {
    if (colon <= 0) {
      httpSendReply(http, 400, "Bad Request", NO_MSG);
      return 0;
    }
    check(colon < lineLength);
    check(http->key        = malloc(colon + 1));
    int i                  = offset;
    for (int j = 0; j < colon; j++) {
      ch                   = httpGetChar(http, buf, bytes, &i);
      if (ch >= 'A' && ch <= 'Z') {
        ch                |= 0x20;
      }
      http->key[j]         = ch;
    }
    http->key[colon]       = '\000';
    char *value;
    check(value            = malloc(lineLength - colon));
    i++;
    int j                  = 0;
    for (int k = 0; k < lineLength - colon - 1; j++, k++) {
      int ch           = httpGetChar(http, buf, bytes, &i);
      if ((ch == ' ' || ch == '\t') && j == 0) {
        j--;
      } else if (ch == '\r' && k == lineLength - colon - 2) {
        break;
      } else {
        value[j]           = ch;
      }
    }
    value[j]               = '\000';
    if (getRefFromHashMap(&http->header, http->key)) {
      debug("Dropping duplicate header \"%s\"", http->key);
      free(http->key);
      free(value);
      http->key            = NULL;
    } else {
      addToHashMap(&http->header, http->key, value);
    }
  }
  return 1;
}

static int httpConsumePayload(struct HttpConnection *http, const char *buf,
                              int len) {
  if (http->expecting >= 0) {
    // If positive, we know the expected length of payload and
    // can keep the connection open.
    // If negative, allow unlimited payload, but close connection
    // when done.
    if (len > http->expecting) {
      len                  = http->expecting;
    }
    http->expecting       -= len;
  }
  if (http->callback) {
    check(!http->done);
    int rc                 = http->callback(http, http->arg, buf, len);
    struct ServerConnection *connection = httpGetServerConnection(http);
    switch (rc) {
    case HTTP_DONE:
    case HTTP_ERROR:
      if (http->expecting < 0 || rc == HTTP_ERROR) {
        httpCloseRead(http);
      }
      http->done           = 1;
      http->isSuspended    = 0;
      http->isPartialReply = 0;
      if (!serverGetTimeout(connection)) {
        serverSetTimeout(connection, CONNECTION_TIMEOUT);
      }
      httpSetState(http, http->expecting ? DISCARD_PAYLOAD : COMMAND);
      break;
    case HTTP_READ_MORE:
      http->isSuspended    = 0;
      http->isPartialReply = 0;
      if (!serverGetTimeout(connection)) {
        serverSetTimeout(connection, CONNECTION_TIMEOUT);
      }
      if (!http->expecting) {
        httpSetState(http, COMMAND);
      }
      break;
    case HTTP_SUSPEND:
      http->isSuspended    = 1;
      http->isPartialReply = 0;
      serverSetTimeout(connection, 0);
      if (http->state != PAYLOAD && http->state != DISCARD_PAYLOAD) {
        check(http->state == HEADERS);
        httpSetState(http, PAYLOAD);
      }
      break;
    case HTTP_PARTIAL_REPLY:
      http->isSuspended    = 0;
      http->isPartialReply = 1;
      if (http->state != PAYLOAD && http->state != DISCARD_PAYLOAD) {
        check(http->state == HEADERS);
        httpSetState(http, PAYLOAD);
      }
      break;
    default:
      check(0);
    }
  } else {
    // If we do not have a callback for handling the payload, and we also do
    // not know how long the payload is (because there was not Content-Length),
    // we now close the connection.
    if (http->expecting < 0) {
      http->expecting      = 0;
      httpCloseRead(http);
      httpSetState(http, COMMAND);
    }
  }
  return len;
}

static int httpParsePayload(struct HttpConnection *http, int offset,
                            const char *buf, int bytes) {
  int consumed               = 0;
  if (offset < 0) {
    check(-offset <= http->partialLength);
    if (http->expecting) {
      consumed               = httpConsumePayload(http,
                                  http->partial + http->partialLength + offset,
                                  -offset);
      if (consumed == http->partialLength) {
        free(http->partial);
        http->partial        = NULL;
        http->partialLength  = 0;
      } else {
        memmove(http->partial, http->partial + consumed,
                http->partialLength - consumed);
        http->partialLength -= consumed;
      }
      offset                += consumed;
    }
  }
  if (http->expecting && bytes - offset > 0) {
    check(offset >= 0);
    consumed                += httpConsumePayload(http, buf + offset,
                                                  bytes - offset);
  }
  return consumed;
}

static int httpHandleWebSocket(struct HttpConnection *http, int offset,
                               const char *buf, int bytes) {
  check(http->websocketHandler);
  int ch                          = 0x00;
  while (bytes > offset) {
    if (http->websocketType & WS_UNDEFINED) {
      ch                          = httpGetChar(http, buf, bytes, &offset);
      check(ch >= 0);
      if (http->websocketType & 0xFF) {
        // Reading another byte of length information.
        if (http->expecting > 0xFFFFFF) {
          return 0;
        }
        http->expecting           = (128 * http->expecting) + (ch & 0x7F);
        if ((ch & 0x80) == 0) {
          // Done reading length information.
          http->websocketType    &= ~WS_UNDEFINED;

          // ch is used to detect when we read the terminating byte in text
          // mode. In binary mode, it must be set to something other than 0xFF.
          ch                      = 0x00;
        }
      } else {
        // Reading first byte of frame.
        http->websocketType       = (ch & 0xFF) | WS_START_OF_FRAME;
        if (ch & 0x80) {
          // For binary data, we have to read the length before we can start
          // processing payload.
          http->websocketType    |= WS_UNDEFINED;
          http->expecting         = 0;
        }
      }
    } else if (http->websocketType & 0x80) {
      // Binary data
      if (http->expecting) {
        if (offset < 0) {
        handle_partial:
          check(-offset <= http->partialLength);
          int len                 = -offset;
          if (len >= http->expecting) {
            len                   = http->expecting;
            http->websocketType  |= WS_END_OF_FRAME;
          }
          if (len &&
              http->websocketHandler(http, http->arg, http->websocketType,
                                  http->partial + http->partialLength + offset,
                                  len) != HTTP_DONE) {
            return 0;
          }

          if (ch == 0xFF) {
            // In text mode, we jump to handle_partial, when we find the
            // terminating 0xFF byte. If so, we should try to consume it now.
            if (len < http->partialLength) {
              len++;
              http->websocketType = WS_UNDEFINED;
            }
          }

          if (len == http->partialLength) {
            free(http->partial);
            http->partial         = NULL;
            http->partialLength   = 0;
          } else {
            memmove(http->partial, http->partial + len,
                    http->partialLength - len);
            http->partialLength  -= len;
          }
          offset                 += len;
          http->expecting        -= len;
        } else {
        handle_buffered:;
          int len                 = bytes - offset;
          if (len >= http->expecting) {
            len                   = http->expecting;
            http->websocketType  |= WS_END_OF_FRAME;
          }
          if (len &&
              http->websocketHandler(http, http->arg, http->websocketType,
                                     buf + offset, len) != HTTP_DONE) {
            return 0;
          }

          if (ch == 0xFF) {
            // In text mode, we jump to handle_buffered, when we find the
            // terminating 0xFF byte. If so, we should consume it now.
            check(offset + len < bytes);
            len++;
            http->websocketType   = WS_UNDEFINED;
          }
          offset                 += len;
          http->expecting        -= len;
        }
        http->websocketType      &= ~(WS_START_OF_FRAME | WS_END_OF_FRAME);
      } else {
        // Read all data. Go back to looking for a new frame header.
        http->websocketType       = WS_UNDEFINED;
      }
    } else {
      // Process text data until we find a 0xFF bytes.
      int i                       = offset;

      // If we have partial data, process that first.
      while (i < 0) {
        ch                        = httpGetChar(http, buf, bytes, &i);
        check(ch != -1);

        // Terminate when we either find the 0xFF, or we have reached the end
        // of partial data.
        if (ch == 0xFF || !i) {
          // Set WS_END_OF_FRAME, iff we have found the 0xFF marker.
          http->expecting         = i - offset - (ch == 0xFF);
          goto handle_partial;
        }
      }

      // Read all remaining buffered bytes (i.e. positive offset).
      while (bytes > i) {
        ch                        = httpGetChar(http, buf, bytes, &i);
        check(ch != -1);

        // Terminate when we either find the 0xFF, or we have reached the end
        // of buffered data.
        if (ch == 0xFF || bytes == i) {
          // Set WS_END_OF_FRAME, iff we have found the 0xFF marker.
          http->expecting         = i - offset - (ch == 0xFF);
          goto handle_buffered;
        }
      }
    }
  }
  return 1;
}

int httpHandleConnection(struct ServerConnection *connection, void *http_,
                         short *events, short revents) {
  struct HttpConnection *http        = (struct HttpConnection *)http_;
  struct Trie *handlers              = serverGetHttpHandlers(http->server);
  http->connection                   = connection;
  int  bytes;
  do {
    bytes                            = 0;
    *events                          = 0;
    char buf[4096];
    int  eof                         = http->closed;
    if ((revents & POLLIN) && !http->closed) {
      bytes                          = httpRead(http, buf, sizeof(buf));
      if (bytes > 0) {
        http->headerLength          += bytes;
        if (http->headerLength > MAX_HEADER_LENGTH) {
          httpSendReply(http, 413, "Header too big", NO_MSG);
          bytes                      = 0;
          eof                        = 1;
        }
      } else {
        if (bytes == 0 || errno != EAGAIN) {
          httpCloseRead(http);
          eof                        = 1;
        } else {
          if (http->sslHndl && http->lastError == SSL_ERROR_WANT_WRITE) {
            *events                 |= POLLOUT;
          }
        }
        bytes                        = 0;
      }
    }

    if (bytes > 0 && http->state == SNIFFING_SSL) {
      // Assume that all legitimate HTTP commands start with a sequence of
      // letters followed by a space character. If we don't see this pattern,
      // or if the method does not match one of the known methods, we try
      // switching to SSL, instead.
      int isSSL                      = 0;
      char method[12]                = { 0 };
      for (int i = -http->partialLength, j = 0, ch;
           (ch = httpGetChar(http, buf, bytes, &i)) != -1;
           j++) {
        if ((j > 0 && (ch == ' ' || ch == '\t')) ||
            ch == '\r' || ch == '\n') {
          isSSL                      = strcmp(method, "OPTIONS") &&
                                       strcmp(method, "GET") &&
                                       strcmp(method, "HEAD") &&
                                       strcmp(method, "POST") &&
                                       strcmp(method, "PUT") &&
                                       strcmp(method, "DELETE") &&
                                       strcmp(method, "TRACE") &&
                                       strcmp(method, "CONNECT");
          http->state                = COMMAND;
          break;
        } else if (j >= (int)sizeof(method)-1 ||
                   ch < 'A' || (ch > 'Z' && ch < 'a') || ch > 'z') {
          isSSL                      = 1;
          http->state                = COMMAND;
          break;
        } else {
          method[j]                  = ch & ~0x20;
        }
      }
      if (isSSL) {
        if (httpPromoteToSSL(http, buf, bytes) < 0) {
          httpCloseRead(http);
          bytes                      = 0;
          eof                        = 1;
        } else {
          http->headerLength         = 0;
          *events                   |= POLLIN;
          continue;
        }
      }
    }

    if (bytes > 0 || (eof && http->partial)) {
      check(!!http->partial == !!http->partialLength);
      int  offset                    = -http->partialLength;
      int  eob                       = 0;
      do {
        int pushBack                 = 0;
        int consumed                 = 0;
        if (http->state == SNIFFING_SSL || http->state == COMMAND ||
            http->state == HEADERS) {
          check(!http->expecting);
          int  lineLength            = 0;
          int  colon                 = -1;
          int  firstSpace            = -1;
          int  lastSpace             = -1;
          int  fullLine              = 1;
          for (int i = offset; ; lineLength++) {
            int ch                   = httpGetChar(http, buf, bytes, &i);
            if (ch == ':') {
              if (colon < 0) {
                colon                = lineLength;
              }
            } else if (ch == ' ' || ch == '\t') {
              if (firstSpace < 0) {
                firstSpace           = lineLength;
              } else {
                lastSpace            = lineLength;
              }
            } else if (ch == '\n') {
              break;
            } else if (ch == -1) {
              fullLine               = 0;
              eob                    = 1;
              break;
            }
          }
          if (fullLine || eof) {
            consumed                 = lineLength + 1;
            if (lineLength) {
              if (http->state == SNIFFING_SSL || http->state == COMMAND) {
                if (!httpParseCommand(http, offset, buf, bytes, firstSpace,
                                      lastSpace, lineLength)) {
                  break;
                }
              } else {
                check(http->state == HEADERS);
                if (!httpParseHeaders(http, handlers, offset, buf, bytes,
                                      colon, lineLength)) {
                  break;
                }
              }
            }
          } else {
            pushBack                 = lineLength;
          }
        } else if (http->state == PAYLOAD ||
                   http->state == DISCARD_PAYLOAD) {
          if (http->expecting) {
            int len                  = bytes - offset;
            if (http->expecting > 0 &&
                len > http->expecting) {
              len                    = http->expecting;
            }
            if (http->state == PAYLOAD) {
              len                    = httpParsePayload(http, offset, buf,
                                                        len + offset);
            }
            consumed                 = len;
            pushBack                 = bytes - offset - len;
          }
        } else if (http->state == WEBSOCKET) {
          if (!httpHandleWebSocket(http, offset, buf, bytes)) {
            httpCloseRead(http);
            break;
          }
          consumed                  += bytes - offset;
        } else {
          check(0);
        }

        offset                      += consumed;
        if (pushBack) {
          check(offset + pushBack == bytes);
          if (offset >= 0) {
            check(http->partial      = realloc(http->partial, pushBack));
            memcpy(http->partial, buf + offset, pushBack);
          } else if (pushBack != http->partialLength) {
            char *partial;
            check(partial            = malloc(pushBack));
            for (int i = offset, j = 0; j < pushBack; j++) {
              partial[j]             = httpGetChar(http, buf, bytes, &i);
            }
            free(http->partial);
            http->partial            = partial;
          }
          http->partialLength        = pushBack;
          offset                     = -pushBack;
          break;
        } else {
          eob                       |= offset >= bytes;
        }
      } while (!eob && !http->closed);
      if (http->closed || offset >= 0) {
        free(http->partial);
        http->partial                = NULL;
        http->partialLength          = 0;
      } else if (-offset != http->partialLength) {
        check(-offset < http->partialLength);
        memmove(http->partial, http->partial + http->partialLength + offset,
                -offset);
        http->partialLength          = -offset;
      }
    }

    // If the peer closed the connection, clean up now.
    if (eof) {
      check(!http->partial);
      switch (http->state) {
      case SNIFFING_SSL:
      case COMMAND:
        break;
      case HEADERS:
        check(!http->expecting);
        http->callback               = NULL;
        http->arg                    = NULL;
        httpHandleCommand(http, handlers);
        httpCloseRead(http);
        httpSetState(http, COMMAND);
        break;
      case PAYLOAD:
      case DISCARD_PAYLOAD:
      case WEBSOCKET:
        http->expecting              = 0;
        httpCloseRead(http);
        httpSetState(http, COMMAND);
        break;
      }
    }

    for (;;) {
      // Try to write any pending outgoing data
      if (http->msg && http->msgLength > 0) {
        int wrote                    = httpWrite(http, http->msg,
                                                 http->msgLength);
        if (wrote < 0 && errno != EAGAIN) {
          httpCloseRead(http);
          free(http->msg);
          http->msgLength            = 0;
          http->msg                  = NULL;
          break;
        } else if (wrote > 0) {
          if (wrote == http->msgLength) {
            free(http->msg);
            http->msgLength          = 0;
            http->msg                = NULL;
          } else {
            memmove(http->msg, http->msg + wrote, http->msgLength - wrote);
            http->msgLength         -= wrote;
          }
        }
        // SSL might require reading in order to write
        else if (wrote < 0 && errno == EAGAIN && http->sslHndl) {
          if (http->lastError == SSL_ERROR_WANT_READ && !http->closed) {
            *events                 |= POLLIN;
          }
        }
      }
  
      // If the callback only provided partial data, refill the outgoing
      // buffer whenever it runs low.
      if (http->isPartialReply && (!http->msg || http->msgLength <= 0)) {
        httpConsumePayload(http, "", 0);
      } else {
        break;
      }
    }

    *events                         |=
      (*events & ~(POLLIN|POLLOUT)) |
      (!http->closed && ((http->state != PAYLOAD &&
                          http->state != DISCARD_PAYLOAD) ||
                         http->expecting) ? POLLIN : 0) |
      (http->msg || http->isPartialReply ? POLLOUT : 0);

    connection                       = httpGetServerConnection(http);
    int timedOut                     = serverGetTimeout(connection) < 0;
    if (timedOut) {
      free(http->partial);
      http->partial                  = NULL;
      http->partialLength            = 0;
      free(http->msg);
      http->msg                      = NULL;
      http->msgLength                = 0;
    }
  
    if ((!(*events || http->isSuspended) || timedOut) && http->sslHndl) {
      *events                        = 0;
      serverSetTimeout(connection, 1);
      int wasAlreadyClosed           = http->closed;
      httpCloseRead(http);
      dcheck(!ERR_peek_error());
      sslBlockSigPipe();
      int rc                         = SSL_shutdown(http->sslHndl);
      switch (rc) {
      case 1:
        sslFreeHndl(&http->sslHndl);
        break;
      case 0:
        if (!wasAlreadyClosed) {
          *events                   |= POLLIN;
        }
        break;
      case -1:
        switch (SSL_get_error(http->sslHndl, rc)) {
        case SSL_ERROR_WANT_READ:
          if (!wasAlreadyClosed) {
            *events                 |= POLLIN;
          }
          break;
        case SSL_ERROR_WANT_WRITE:
          *events                   |= POLLOUT;
          break;
        }
        break;
      }
      ERR_clear_error();
      dcheck(!ERR_peek_error());
      if (sslUnblockSigPipe()) {
        *events                      = 0;
        sslFreeHndl(&http->sslHndl);
      }
    } else if (!http->sslHndl && timedOut) {
      *events                        = 0;
      serverSetTimeout(connection, 0);
      httpCloseRead(http);
    }
    revents                          = POLLIN | POLLOUT;
  } while (bytes > 0 && *events & POLLIN && !http->closed);
  return (*events & (POLLIN|POLLOUT)) ||
         (!http->closed && http->isSuspended);
}

void httpSetCallback(struct HttpConnection *http,
                     int (*callback)(struct HttpConnection *, void *,
                                     const char *, int), void *arg) {
  http->callback = callback;
  http->arg      = arg;
}

void *httpGetPrivate(struct HttpConnection *http) {
  return http->private;
}

void *httpSetPrivate(struct HttpConnection *http, void *private) {
  void *old     = http->private;
  http->private = private;
  return old;
}

void httpSendReply(struct HttpConnection *http, int code,
                   const char *msg, const char *fmt, ...) {
  http->code     = code;
  char *body;
  char *title    = code != 200 ? stringPrintf(NULL, "%d %s", code, msg) : NULL;
  char *details  = NULL;
  if (fmt != NULL && strcmp(fmt, NO_MSG)) {
    va_list ap;
    va_start(ap, fmt);
    details      = vStringPrintf(NULL, fmt, ap);
    va_end(ap);
  }
  body           = stringPrintf(NULL,
     "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
     "<!DOCTYPE html PUBLIC "
               "\"-//W3C//DTD XHTML 1.0 Transitional//EN\" "
               "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
     "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
     "xmlns:v=\"urn:schemas-microsoft-com:vml\" "
     "xml:lang=\"en\" lang=\"en\">\n"
     "<head>\n"
     "<title>%s</title>\n"
     "</head>\n"
     "<body>\n"
     "%s\n"
     "</body>\n"
     "</html>\n",
     title ? title : msg, fmt && strcmp(fmt, NO_MSG) ? details : msg);
  free(details);
  free(title);
  char *response = NULL;
  if (code) {
    response     = stringPrintf(NULL,
                                "HTTP/1.1 %d %s\r\n"
                                "%s"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "Content-Length: %ld\r\n"
                                "\r\n",
                                code, msg,
                                code != 200 ? "Connection: close\r\n" : "",
                                (long)strlen(body));
  }
  int isHead     = !strcmp(http->method, "HEAD");
  if (!isHead) {
    response     = stringPrintf(response, "%s", body);
  }
  free(body);
  httpTransfer(http, response, strlen(response));
  if (code != 200 || isHead) {
    httpCloseRead(http);
  }
}

void httpSendWebSocketTextMsg(struct HttpConnection *http, int type,
                              const char *fmt, ...) {
  check(type >= 0 && type <= 0x7F);
  va_list ap;
  va_start(ap, fmt);
  char *buf;
  int len;
  if (strcmp(fmt, BINARY_MSG)) {
    // Send a printf() style text message
    buf              = vStringPrintf(NULL, fmt, ap);
    len              = strlen(buf);
  } else {
    // Send a binary message
    len              = va_arg(ap, int);
    buf              = va_arg(ap, char *);
  }
  va_end(ap);
  check(len >= 0 && len < 0x60000000);

  // We assume that all input data is directly mapped in the range 0..255
  // (e.g. ISO-8859-1). In order to transparently send it over a web socket,
  // we have to encode it in UTF-8.
  int utf8Len        = len + 2;
  for (int i = 0; i < len; ++i) {
    if (buf[i] & 0x80) {
      ++utf8Len;
    }
  }
  char *utf8;
  check(utf8         = malloc(utf8Len));
  utf8[0]            = type;
  for (int i = 0, j = 1; i < len; ++i) {
    unsigned char ch = buf[i];
    if (ch & 0x80) {
      utf8[j++]      = 0xC0 + (ch >> 6);
      utf8[j++]      = 0x80 + (ch & 0x3F);
    } else {
      utf8[j++]      = ch;
    }
    check(j < utf8Len);
  }
  utf8[utf8Len-1]    = '\xFF';

  // Free our temporary buffer, if we actually did allocate one.
  if (strcmp(fmt, BINARY_MSG)) {
    free(buf);
  }

  // Send to browser.
  httpTransfer(http, utf8, utf8Len);
}

void httpSendWebSocketBinaryMsg(struct HttpConnection *http, int type,
                                const void *buf, int len) {
  check(type >= 0x80 && type <= 0xFF);
  check(len > 0 && len < 0x7FFFFFF0);

  // Allocate buffer for header and payload.
  char *data;
  check(data  = malloc(len + 6));
  data[0]     = type;

  // Convert length to base-128.
  int i       = 0;
  int l       = len;
  do {
    data[++i] = 0x80 + (l & 0x7F);
    l        /= 128;
  } while (l);
  data[i]    &= 0x7F;

  // Reverse digits, so that they are big-endian.
  for (int j = 0; j < i/2; ++j) {
    char ch   = data[1+j];
    data[1+j] = data[i-j];
    data[i-j] = ch;
  }

  // Transmit header and payload.
  memmove(data + i + 1, buf, len);
  httpTransfer(http, data, len + i + 1);
}

void httpExitLoop(struct HttpConnection *http, int exitAll) {
  serverExitLoop(http->server, exitAll);
}

struct Server *httpGetServer(const struct HttpConnection *http) {
  return http->server;
}

struct ServerConnection *httpGetServerConnection(const struct HttpConnection *
                                                 http) {
  struct HttpConnection *httpW = (struct HttpConnection *)http;
  httpW->connection = serverGetConnection(http->server, http->connection,
                                          http->fd);
  return http->connection;
}

int httpGetFd(const HttpConnection *http) {
  return http->fd;
}

const char *httpGetPeerName(const struct HttpConnection *http) {
  return http->peerName;
}

const char *httpGetMethod(const struct HttpConnection *http) {
  return http->method;
}

const char *httpGetProtocol(const struct HttpConnection *http) {
  return http->sslHndl ? "https" : "http";
}

const char *httpGetHost(const struct HttpConnection *http) {
  const char *host = getFromHashMap(&http->header, "host");
  if (!host || !*host) {
    host           = "localhost";
  }
  return host;
}

int httpGetPort(const struct HttpConnection *http) {
  return http->port;
}

const char *httpGetPath(const struct HttpConnection *http) {
  return http->matchedPath;
}

const char *httpGetPathInfo(const struct HttpConnection *http) {
  return http->pathInfo ? http->pathInfo : "";
}

const char *httpGetQuery(const struct HttpConnection *http) {
  return http->query ? http->query : "";
}

const char *httpGetURL(const struct HttpConnection *http) {
  if (!http->url) {
    const char *host           = httpGetHost(http);
    int s_size                 = 8 + strlen(host) + 25 + strlen(http->path);
    check(*(char **)&http->url = malloc(s_size + 1));
    *http->url                 = '\000';
    strncat(http->url, http->sslHndl ? "https://" : "http://", s_size);
    strncat(http->url, host, s_size);
    if (http->port != (http->sslHndl ? 443 : 80)) {
      snprintf(strrchr(http->url, '\000'), 25, ":%d", http->port);
    }
    strncat(http->url, http->path, s_size);
  }
  return http->url;
}

const char *httpGetVersion(const struct HttpConnection *http) {
  return http->version;
}

const struct HashMap *httpGetHeaders(const struct HttpConnection *http) {
  return &http->header;
}
