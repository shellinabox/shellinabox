// server.h -- Generic server that can deal with HTTP connections
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

#ifndef SERVER_H__
#define SERVER_H__

#include <time.h>

#include "libhttp/trie.h"
#include "libhttp/http.h"
#include "libhttp/ssl.h"

struct Server;

struct ServerConnection {
  int                   deleted;
  time_t                timeout;
  int                   (*handleConnection)(struct ServerConnection *c,
                                            void *arg, short *events,
                                            short revents);
  void                  (*destroyConnection)(void *arg);
  void                  *arg;
};

struct Server {
  int                     port;
  int                     looping;
  int                     exitAll;
  int                     serverTimeout;
  int                     serverFd;
  int                     numericHosts;
  struct pollfd           *pollFds;
  struct ServerConnection *connections;
  int                     numConnections;
  struct Trie             handlers;
  struct SSLSupport       ssl;
};

struct Server *newCGIServer(int localhostOnly, int portMin, int portMax,
                            int timeout);
struct Server *newServer(int localhostOnly, int port);
void initServer(struct Server *server, int localhostOnly, int portMin,
                int portMax, int timeout);
void destroyServer(struct Server *server);
void deleteServer(struct Server *server);
int  serverGetListeningPort(struct Server *server);
int  serverGetFd(struct Server *server);
void serverRegisterHttpHandler(struct Server *server, const char *url,
                               int (*handler)(struct HttpConnection *, void *,
                                              const char *, int), void *arg);
void serverRegisterStreamingHttpHandler(struct Server *server, const char *url,
                               int (*handler)(struct HttpConnection *, void *),
                               void *arg);
void serverRegisterWebSocketHandler(struct Server *server, const char *url,
       int (*handler)(struct HttpConnection *, void *, int, const char *, int),
       void *arg);
struct ServerConnection *serverAddConnection(struct Server *server, int fd,
                            int (*handleConnection)(struct ServerConnection *c,
                                                    void *arg, short *events,
                                                    short revents),
                            void (*destroyConnection)(void *arg),
                            void *arg);
void serverDeleteConnection(struct Server *server, int fd);
void serverSetTimeout(struct ServerConnection *connection, time_t timeout);
time_t serverGetTimeout(struct ServerConnection *connection);
struct ServerConnection *serverGetConnection(struct Server *server,
                                             struct ServerConnection *hint,
                                             int fd);
short serverConnectionSetEvents(struct Server *server,
                                struct ServerConnection *connection, int fd,
                                short events);
void serverExitLoop(struct Server *server, int exitAll);
void serverLoop(struct Server *server);
void serverEnableSSL(struct Server *server, int flag);
void serverSetCertificate(struct Server *server, const char *filename,
                          int autoGenerateMissing);
void serverSetCertificateFd(struct Server *server, int fd);
void serverSetNumericHosts(struct Server *server, int numericHosts);
struct Trie *serverGetHttpHandlers(struct Server *server);

extern time_t currentTime;

#endif
