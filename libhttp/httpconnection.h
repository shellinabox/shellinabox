// httpconnection.h -- Manage state machine for HTTP connections
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

#ifndef HTTP_CONNECTION__
#define HTTP_CONNECTION__

#include <time.h>

#include "libhttp/hashmap.h"
#include "libhttp/trie.h"
#include "libhttp/server.h"
#include "libhttp/http.h"
#include "libhttp/ssl.h"

#define HTTP_DONE          0
#define HTTP_ERROR         1
#define HTTP_READ_MORE     2
#define HTTP_SUSPEND       3
#define HTTP_PARTIAL_REPLY 4

#define WS_UNDEFINED       0x1000
#define WS_START_OF_FRAME  0x0100
#define WS_END_OF_FRAME    0x0200

#define NO_MSG             "\001"

struct HttpConnection {
  struct Server           *server;
  struct ServerConnection *connection;
  int                     fd;
  int                     port;
  int                     closed;
  int                     isSuspended;
  int                     isPartialReply;
  int                     done;
  enum { SNIFFING_SSL, COMMAND, HEADERS, PAYLOAD, DISCARD_PAYLOAD,
         WEBSOCKET } state;
  char                    *peerName;
  int                     peerPort;
  char                    *url;
  char                    *method;
  char                    *path;
  char                    *matchedPath;
  char                    *pathInfo;
  char                    *query;
  char                    *version;
  struct HashMap          header;
  int                     headerLength;
  char                    *key;
  char                    *partial;
  int                     partialLength;
  char                    *msg;
  int                     msgLength;
  int                     msgOffset;
  int                     totalWritten;
  int                     expecting;
  int                     websocketType;
  int                     (*callback)(struct HttpConnection *, void *,
                                      const char *,int);
  int                     (*websocketHandler)(struct HttpConnection *, void *,
                                              int, const char *, int);
  void                    *arg;
  void                    *private;
  int                     code;
  struct SSLSupport       *ssl;
  SSL                     *sslHndl;
  int                     lastError;
};

struct HttpHandler {
  int (*handler)(struct HttpConnection *, void *);
  int (*streamingHandler)(struct HttpConnection *, void *, const char *, int);
  int (*websocketHandler)(struct HttpConnection *, void *, int,
                          const char *, int);
  void *arg, *streamingArg;
  
};

struct HttpConnection *newHttpConnection(struct Server *server, int fd,
                                         int port, struct SSLSupport *ssl,
                                         int numericHosts);
void initHttpConnection(struct HttpConnection *http, struct Server *server,
                        int fd, int port, struct SSLSupport *ssl,
                        int numericHosts);
void destroyHttpConnection(struct HttpConnection *http);
void deleteHttpConnection(struct HttpConnection *http);
void httpTransfer(struct HttpConnection *http, char *msg, int len);
void httpTransferPartialReply(struct HttpConnection *http, char *msg, int len);
int httpHandleConnection(struct ServerConnection *connection, void *http_,
                         short *events, short revents);
void httpSetCallback(struct HttpConnection *http,
                     int (*callback)(struct HttpConnection *, void *,
                                     const char *, int), void *arg);
void *httpGetPrivate(struct HttpConnection *http);
void *httpSetPrivate(struct HttpConnection *http, void *private);
void httpSendReply(struct HttpConnection *http, int code,
                   const char *msg, const char *fmt, ...)
  __attribute__((format(printf, 4, 5)));
void httpSendWebSocketTextMsg(struct HttpConnection *http, int type,
                              const char *fmt, ...)
  __attribute__((format(printf, 3, 4)));
void httpSendWebSocketBinaryMsg(struct HttpConnection *http, int type,
                                const void *buf, int len);
void httpExitLoop(struct HttpConnection *http, int exitAll);
struct Server *httpGetServer(const struct HttpConnection *http);
struct ServerConnection *httpGetServerConnection(const struct HttpConnection*);
int         httpGetFd(const HttpConnection *http);
const char *httpGetPeerName(const struct HttpConnection *http);
const char *httpGetMethod(const struct HttpConnection *http);
const char *httpGetProtocol(const struct HttpConnection *http);
const char *httpGetHost(const struct HttpConnection *http);
int         httpGetPort(const struct HttpConnection *http);
const char *httpGetPath(const struct HttpConnection *http);
const char *httpGetPathInfo(const struct HttpConnection *http);
const char *httpGetQuery(const struct HttpConnection *http);
const char *httpGetURL(const struct HttpConnection *http);
const char *httpGetVersion(const struct HttpConnection *http);
const struct HashMap *httpGetHeaders(const struct HttpConnection *http);

#endif /* HTTP_CONNECTION__ */
