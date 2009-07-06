// url.h -- Object representing uniform resource locators
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

#ifndef URL_H__
#define URL_H__

#include "libhttp/http.h"

#include "libhttp/hashmap.h"
#include "libhttp/httpconnection.h"

struct URL {
  char           *protocol;
  char           *user;
  char           *password;
  char           *host;
  int            port;
  char           *path;
  char           *pathinfo;
  char           *query;
  char           *anchor;
  char           *url;
  struct HashMap args;
};

struct URL *newURL(const struct HttpConnection *http,
                   const char *buf, int len);
void initURL(struct URL *url, const struct HttpConnection *http,
             const char *buf, int len);
void destroyURL(struct URL *url);
void deleteURL(struct URL *url);
const char *urlGetProtocol(struct URL *url);
const char *urlGetUser(struct URL *url);
const char *urlGetPassword(struct URL *url);
const char *urlGetHost(struct URL *url);
int         urlGetPort(struct URL *url);
const char *urlGetPath(struct URL *url);
const char *urlGetPathInfo(struct URL *url);
const char *urlGetQuery(struct URL *url);
const char *urlGetAnchor(struct URL *url);
const char *urlGetURL(struct URL *url);
const struct HashMap *urlGetArgs(struct URL *url);

#endif /* URL_H__ */
