// privileges.h -- Manage process privileges
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

#ifndef PRIVILEGES_H__
#define PRIVILEGES_H__

#include <sys/types.h>

extern int   runAsUser;
extern int   runAsGroup;

void lowerPrivileges(void);
void dropPrivileges(void);
const char *getUserName(uid_t uid);
uid_t getUserId(const char *name);
uid_t parseUserArg(const char *arg, const char **name);
const char *getGroupName(gid_t gid);
gid_t getGroupId(const char *name);
gid_t parseGroupArg(const char *arg, const char **name);

#ifndef HAVE_GETRESUID
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
#endif
#ifndef HAVE_GETRESGID
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
#endif
#ifndef HAVE_SETRESUID
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
#endif
#ifndef HAVE_SETRESGID
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
#endif

#endif
