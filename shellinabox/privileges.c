// privileges.c -- Manage process privileges
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

#define _GNU_SOURCE
#include "config.h"

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "shellinabox/privileges.h"
#include "logging/logging.h"

int   runAsUser  = -1;
int   runAsGroup = -1;


static void removeGroupPrivileges(int showError) {
  gid_t rg, eg, sg;
  check(!getresgid(&rg, &eg, &sg));

  // Remove all supplementary groups. Allow this command to fail. That could
  // happen if we run as an unprivileged user.
  setgroups(0, (gid_t *)"");

  if (runAsGroup >= 0) {
    uid_t ru, eu, su;
    getresuid(&ru, &eu, &su);

    // Try to switch the user-provided group.
    if ((ru && runAsGroup != rg) ||
        setresgid(runAsGroup, runAsGroup, runAsGroup)) {
      if (showError) {
        fatal("Only privileged users can change their group memberships");
      } else {
        _exit(1);
      }
    }
  } else {
    if (rg) {
      // If we were started as a set-gid binary, drop these permissions, now.
      check(!setresgid(rg, rg, rg));
    } else {
      // If we are running as root, switch to "nogroup"
      gid_t ng = getGroupId("nogroup");
      check(!setresgid(ng, ng, ng));
    }
  }
}

void lowerPrivileges(void) {
  uid_t r, e, g;
  check(!getresuid(&r, &e, &g));

  // Permanently lower all group permissions. We do not actually need these,
  // as we still have "root" user privileges in our saved-uid.
  removeGroupPrivileges(0);

  // Temporarily lower user privileges. If we used to have "root" privileges,
  // we can later still regain them.
  setresuid(-1, -1, 0);

  if (runAsUser >= 0) {
    // Try to switch to the user-provided user id.
    if (r && runAsUser != r) {
      fatal("Only privileged users can change their user id");
    }
    check(!setresuid(runAsUser, runAsUser, -1));
  } else {
    if (r) {
      // If we were started as a set-uid binary, temporarily lower these
      // permissions.
      check(!setresuid(r, r, -1));
    } else {
      // If we are running as "root", temporarily switch to "nobody".
      uid_t n = getUserId("nobody");
      check(!setresuid(n, n, -1));
    }
  }
}

void dropPrivileges(void) {
  uid_t r, e, s;
  check(!getresuid(&r, &e, &s));

  // Drop all group privileges.
  removeGroupPrivileges(1);

  if (runAsUser >= 0) {
    // Try to switch to the user-provided user id.
    if ((r && runAsUser != r) ||
        setresuid(runAsUser, runAsUser, runAsUser)) {
      fatal("Only privileged users can change their user id.");
    }
  } else {
    if (r) {
      // If we were started as a set-uid binary, permanently drop these
      // permissions.
      check(!setresuid(r, r, r));
    } else {
      // If we are running as "root", permanently switch to "nobody".
      uid_t n = getUserId("nobody");
      check(!setresuid(n, n, n));
    }
  }
}

const char *getUserName(uid_t uid) {
  struct passwd pwbuf, *pw;
  char *buf;
  int len      = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (len <= 0) {
    len        = 4096;
  }
  check(buf    = malloc(len));
  char *user;
  if (getpwuid_r(uid, &pwbuf, buf, len, &pw) || !pw) {
    check(user = malloc(32));
    snprintf(user, 32, "%d", uid);
  } else {
    check(user = strdup(pw->pw_name));
  }
  free(buf);
  return user;
}

uid_t getUserId(const char *name) {
  struct passwd pwbuf, *pw;
  char *buf;
  int len   = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (len <= 0) {
    len     = 4096;
  }
  check(buf = malloc(len));
  if (getpwnam_r(name, &pwbuf, buf, len, &pw) || !pw) {
    fatal("Cannot look up user id \"%s\"", name);
  }
  uid_t uid = pw->pw_uid;
  free(buf);
  return uid;
}

uid_t parseUser(const char *arg, const char **name) {
  char *end;
  errno           = 0;
  unsigned long l = strtoul(arg, &end, 10);
  if (errno || l > INT_MAX || *end) {
    if (name) {
      check(*name = strdup(arg));
    }
    return getUserId(arg);
  } else {
    if (name) {
      *name       = getUserName((uid_t)l);
    }
    return (uid_t)l;
  }
}

const char *getGroupName(gid_t gid) {
  struct group grbuf, *gr;
  char *buf;
  int len       = sysconf(_SC_GETGR_R_SIZE_MAX);
  if (len <= 0) {
    len         = 4096;
  }
  check(buf     = malloc(len));
  char *group;
  if (getgrgid_r(gid, &grbuf, buf, len, &gr) || !gr) {
    check(group = malloc(32));
    snprintf(group, 32, "%d", gid);
  } else {
    check(group = strdup(gr->gr_name));
  }
  free(buf);
  return group;
}

gid_t getGroupId(const char *name) {
  struct group grbuf, *gr;
  char *buf;
  int gr_len      = sysconf(_SC_GETGR_R_SIZE_MAX);
  if (gr_len <= 0) {
    gr_len        = 4096;
  }
  check(buf       = malloc(gr_len));
  if (getgrnam_r(name, &grbuf, buf, gr_len, &gr) || !gr) {
    // Maybe, this system does not have a "nogroup" group. Substitute the
    // group of the "nobody" user.
    if (!strcmp(name, "nogroup")) {
      struct passwd pwbuf, *pw;
      int pw_len  = sysconf(_SC_GETPW_R_SIZE_MAX);
      if (pw_len <= 0) {
        pw_len    = 4096;
      }
      if (pw_len > gr_len) {
        check(buf = realloc(buf, pw_len));
      }
      if (!getpwnam_r("nobody", &pwbuf, buf, pw_len, &pw) && pw) {
        debug("Substituting \"nobody's\" primary group for \"nogroup\"");
        gid_t gid = pw->pw_gid;
        free(buf);
        return gid;
      }
    }
    fatal("Cannot look up group \"%s\"", name);
  }
  gid_t gid = gr->gr_gid;
  free(buf);
  return gid;
}

gid_t parseGroup(const char *arg, const char **name) {
  char *end;
  errno           = 0;
  unsigned long l = strtoul(arg, &end, 10);
  if (errno || l > INT_MAX || *end) {
    if (name) {
      check(*name = strdup(arg));
    }
    return getGroupId(arg);
  } else {
    if (name) {
      *name       = getGroupName((uid_t)l);
    }
    return (uid_t)l;
  }
}
