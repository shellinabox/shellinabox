// http.h -- Library for implementing embedded custom HTTP servers
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

#ifndef LIB_HTTP_H__
#define LIB_HTTP_H__

#include <errno.h>
#include <stdarg.h>
#include <time.h>

#define HTTP_DONE          0
#define HTTP_ERROR         1
#define HTTP_READ_MORE     2
#define HTTP_SUSPEND       3
#define HTTP_PARTIAL_REPLY 4

#define WS_START_OF_FRAME    0x0100
#define WS_END_OF_FRAME      0x0200
#define WS_CONNECTION_OPENED 0xFF00
#define WS_CONNECTION_CLOSED 0x7F00

#define NO_MSG             "\001"
#define BINARY_MSG         "\001%d%p"

#define NOINTR(x) ({ int i__; while ((i__ = (x)) < 0 && errno == EINTR); i__;})

typedef struct HashMap HashMap;
typedef struct HttpConnection HttpConnection;
typedef struct ServerConnection ServerConnection;
typedef struct Server Server;
typedef struct URL URL;

Server *newCGIServer(int localhostOnly, int portMin, int portMax, int timeout);
Server *newServer(int localhostOnly, int port);
void deleteServer(Server *server);
int  serverGetListeningPort(Server *server);
int  serverGetFd(Server *server);
void serverRegisterHttpHandler(Server *server, const char *url,
                               int (*handler)(HttpConnection *, void *,
                                              const char *, int), void *arg);
void serverRegisterStreamingHttpHandler(Server *server, const char *url,
                               int (*handler)(HttpConnection *, void *),
                               void *arg);
void serverRegisterWebSocketHandler(Server *server, const char *url,
              int (*handler)(HttpConnection *, void *, int, const char *, int),
              void *arg);
ServerConnection *serverAddConnection(Server *server, int fd,
                              int (*handleConnection)(ServerConnection *,
                                                      void *arg, short *events,
                                                      short revents),
                              void (*destroyConnection)(void *arg),
                              void *arg);
void serverDeleteConnection(Server *server, int fd);
void serverSetTimeout(ServerConnection *connection, time_t timeout);
time_t serverGetTimeout(ServerConnection *connection);
ServerConnection *serverGetConnection(Server *server, ServerConnection *hint,
                                      int fd);
short serverConnectionSetEvents(Server *server, ServerConnection *connection,
                                int fd, short events);
void serverExitLoop(Server *server, int exitAll);
void serverLoop(Server *server);
int  serverSupportsSSL();
void serverEnableSSL(Server *server, int flag);
void serverSetCertificate(Server *server, const char *filename,
                          int autoGenerateMissing);
void serverSetCertificateFd(Server *server, int fd);
void serverSetNumericHosts(Server *server, int numericHosts);

void httpTransfer(HttpConnection *http, char *msg, int len);
void httpTransferPartialReply(HttpConnection *http, char *msg, int len);
void httpSetCallback(HttpConnection *http,
                     int (*callback)(HttpConnection *, void *,
                                     const char *, int), void *arg);
void *httpGetPrivate(HttpConnection *http);
void *httpSetPrivate(HttpConnection *http, void *private);
void httpSendReply(HttpConnection *http, int code,
                   const char *msg, const char *fmt, ...)
  __attribute__((format(printf, 4, 5)));
void httpSendWebSocketTextMsg(HttpConnection *http, int type, const char *fmt,
                              ...) __attribute__((format(printf, 3, 4)));
void httpSendWebSocketBinaryMsg(HttpConnection *http, int type,
                                const void *buf, int len);
void httpExitLoop(HttpConnection *http, int exitAll);
Server *httpGetServer(const HttpConnection *http);
ServerConnection *httpGetServerConnection(const HttpConnection *);
int httpGetFd(const HttpConnection *http);
const char *httpGetPeerName(const HttpConnection *http);
const char *httpGetMethod(const HttpConnection *http);
const char *httpGetVersion(const HttpConnection *http);
const HashMap *httpGetHeaders(const HttpConnection *http);
URL *newURL(const HttpConnection *http, const char *buf, int len);
void deleteURL(URL *url);
const char *urlGetProtocol(URL *url);
const char *urlGetUser(URL *url);
const char *urlGetPassword(URL *url);
const char *urlGetHost(URL *url);
int         urlGetPort(URL *url);
const char *urlGetPath(URL *url);
const char *urlGetPathInfo(URL *url);
const char *urlGetQuery(URL *url);
const char *urlGetAnchor(URL *url);
const char *urlGetURL(URL *url);
const HashMap *urlGetArgs(URL *url);
HashMap *newHashMap(void (*destructor)(void *arg, char *key, char *value),
                    void *arg);
void deleteHashMap(HashMap *hashmap);
const void *addToHashMap(HashMap *hashmap, const char *key, const char *value);
void deleteFromHashMap(HashMap *hashmap, const char *key);
char **getRefFromHashMap(const HashMap *hashmap, const char *key);
const char *getFromHashMap(const HashMap *hashmap, const char *key);
void iterateOverHashMap(struct HashMap *hashmap,
                        int (*fnc)(void *arg, const char *key, char **value),
                        void *arg);
int getHashmapSize(const HashMap *hashmap);

#endif /* LIB_HTTP_H__ */
