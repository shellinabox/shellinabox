// service.h -- Service descriptions
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

#ifndef SERVICE_H__
#define SERVICE_H__

#include "libhttp/hashmap.h"

struct Service {
  int        id;
  const char *path;
  int        useLogin;
  int        useHomeDir;
  int        authUser;
  int        useDefaultShell;
  int        uid; /* Can be -1 */
  int        gid; /* Can be -1 */
  const char *user;
  const char *group;
  const char *cwd;
  const char *cmdline;
};

extern struct Service **services;
extern int            numServices;

void initService(struct Service *service, const char *arg);
struct Service *newService(const char *arg);
void destroyService(struct Service *service);
void deleteService(struct Service *service);
void destroyServiceHashEntry(void *arg, char *key, char *value);
void enumerateServices(HashMap *serviceTable);

#endif
