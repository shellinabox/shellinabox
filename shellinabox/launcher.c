// launcher.c -- Launch services from a privileged process
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

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>
#include <utmpx.h>

#if defined(HAVE_SECURITY_PAM_APPL_H) && defined(HAVE_SECURITY_PAM_MISC_H)
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#else
struct pam_message;
struct pam_response;
struct pam_conv;
typedef struct pam_handle pam_handle_t;
#endif

#if defined(HAVE_PTHREAD_H)
#include <pthread.h>
extern int pthread_once(pthread_once_t *, void (*)(void))__attribute__((weak));
#endif

#include "shellinabox/launcher.h"
#include "shellinabox/privileges.h"
#include "shellinabox/service.h"
#include "libhttp/hashmap.h"
#include "logging/logging.h"

// If PAM support is available, take advantage of it. Otherwise, silently fall
// back on legacy operations for session management.
static int (*x_pam_acct_mgmt)(pam_handle_t *, int);
static int (*x_pam_authenticate)(pam_handle_t *, int);
static int (*x_pam_close_session)(pam_handle_t *, int);
static int (*x_pam_end)(pam_handle_t *, int);
static int (*x_pam_get_item)(const pam_handle_t *, int, const void **);
static int (*x_pam_open_session)(pam_handle_t *, int);
static int (*x_pam_set_item)(pam_handle_t *, int, const void *);
static int (*x_pam_start)(const char *, const char *, const struct pam_conv *,
                          pam_handle_t **);
static int (*x_misc_conv)(int, const struct pam_message **,
                          struct pam_response **, void *);

// Older versions of glibc might not support fdopendir(). That's OK, we can
// work around the lack of it, at a small performance loss.
extern DIR *fdopendir(int) __attribute__((weak));

static int   launcher = -1;
static uid_t restricted;


static void *loadSymbol(const char *lib, const char *fn) {
  void *dl = RTLD_DEFAULT;
  void *rc = dlsym(dl, fn);
  if (!rc) {
    dl     = dlopen(lib, RTLD_LAZY|RTLD_GLOBAL|RTLD_NOLOAD);
    if (dl == NULL) {
      dl   = dlopen(lib, RTLD_LAZY|RTLD_GLOBAL);
    }
    if (dl != NULL) {
      rc   = dlsym(dl, fn);
    }
  }
  return rc;
}

static void loadPAM(void) {
  check(!x_pam_start);
  check(!x_misc_conv);
  struct {
    union {
      void     *avoid_gcc_warning_about_type_punning;
      void     **var;
    };
    const char *lib;
    const char *fn;
  } symbols[] = {
    { { &x_pam_acct_mgmt },     "libpam.so",      "pam_acct_mgmt"     },
    { { &x_pam_authenticate },  "libpam.so",      "pam_authenticate"  },
    { { &x_pam_close_session }, "libpam.so",      "pam_close_session" },
    { { &x_pam_end },           "libpam.so",      "pam_end"           },
    { { &x_pam_get_item },      "libpam.so",      "pam_get_item"      },
    { { &x_pam_open_session },  "libpam.so",      "pam_open_session"  },
    { { &x_pam_set_item },      "libpam.so",      "pam_set_item"      },
    { { &x_pam_start },         "libpam.so",      "pam_start"         },
    { { &x_misc_conv },         "libpam_misc.so", "misc_conv"         }
  };
  for (int i = 0; i < sizeof(symbols)/sizeof(symbols[0]); i++) {
    if (!(*symbols[i].var = loadSymbol(symbols[i].lib, symbols[i].fn))) {
      debug("Failed to load PAM support. Could not find \"%s\"",
            symbols[i].fn);
      for (int j = 0; j < sizeof(symbols)/sizeof(symbols[0]); j++) {
        *symbols[j].var = NULL;
      }
      break;
    }
  }
  debug("Loaded PAM suppport");
}

int supportsPAM(void) {
#if defined(HAVE_SECURITY_PAM_APPL_H) && defined(HAVE_SECURITY_PAM_MISC_H)

  // We want to call loadPAM() exactly once. For single-threaded applications,
  // this is straight-forward. For threaded applications, we need to call
  // pthread_once(), instead. We perform run-time checks for whether we are
  // single- or multi-threaded, so that the same code can be used.
#if defined(HAVE_PTHREAD_H)
  if (!!&pthread_once) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, loadPAM);
  } else
#endif
  {
    static int initialized;
    if (!initialized) {
      initialized = 1;
      loadPAM();
    }
  }
  return x_misc_conv && x_pam_start;
#else
  return 0;
#endif
}

int launchChild(int service, struct Session *session) {
  if (launcher < 0) {
    errno              = EINVAL;
    return -1;
  }

  struct LaunchRequest request = {
    .service           = service,
    .width             = session->width,
    .height            = session->height };
  strncat(request.peerName, httpGetPeerName(session->http),
          sizeof(request.peerName));
  if (NOINTR(write(launcher, &request, sizeof(request))) != sizeof(request)) {
    return -1;
  }
  pid_t pid;
  char cmsg_buf[CMSG_SPACE(sizeof(int))];
  struct iovec iov     = { 0 };
  struct msghdr msg    = { 0 };
  iov.iov_base         = &pid;
  iov.iov_len          = sizeof(pid);
  msg.msg_iov          = &iov;
  msg.msg_iovlen       = 1;
  msg.msg_control      = &cmsg_buf;
  msg.msg_controllen   = sizeof(cmsg_buf);
  int bytes            = NOINTR(recvmsg(launcher, &msg, 0));
  if (bytes < 0) {
    return -1;
  }
  check(bytes == sizeof(pid));
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  check(cmsg);
  check(cmsg->cmsg_level == SOL_SOCKET);
  check(cmsg->cmsg_type  == SCM_RIGHTS);
  session->pty         = *(int *)CMSG_DATA(cmsg);
  return pid;
}

struct Utmp {
  const char   pid[32];
  int          pty;
  int          useLogin;
  struct utmpx utmpx;
};

static HashMap *childProcesses;

void initUtmp(struct Utmp *utmp, int useLogin, const char *ptyPath,
              const char *peerName) {
  memset(utmp, 0, sizeof(struct Utmp));
  utmp->pty                 = -1;
  utmp->useLogin            = useLogin;
  utmp->utmpx.ut_type       = useLogin ? LOGIN_PROCESS : USER_PROCESS;
  dcheck(!strncmp(ptyPath, "/dev/pts", 8));
  strncat(&utmp->utmpx.ut_line[0], ptyPath + 5,   sizeof(utmp->utmpx.ut_line));
  strncat(&utmp->utmpx.ut_id[0],   ptyPath + 8,   sizeof(utmp->utmpx.ut_id));
  strncat(&utmp->utmpx.ut_user[0], "SHELLINABOX", sizeof(utmp->utmpx.ut_user));
  strncat(&utmp->utmpx.ut_host[0], peerName,      sizeof(utmp->utmpx.ut_host));
  struct timeval tv;
  check(!gettimeofday(&tv, NULL));
  utmp->utmpx.ut_tv.tv_sec  = tv.tv_sec;
  utmp->utmpx.ut_tv.tv_usec = tv.tv_usec;
}

struct Utmp *newUtmp(int useLogin, const char *ptyPath,
                     const char *peerName) {
  struct Utmp *utmp;
  check(utmp = malloc(sizeof(struct Utmp)));
  initUtmp(utmp, useLogin, ptyPath, peerName);
  return utmp;
}

void destroyUtmp(struct Utmp *utmp) {
  if (utmp) {
    if (utmp->pty >= 0) {
      utmp->utmpx.ut_type = DEAD_PROCESS;
      memset(&utmp->utmpx.ut_user, 0, sizeof(utmp->utmpx.ut_user));
      memset(&utmp->utmpx.ut_host, 0, sizeof(utmp->utmpx.ut_host));
      struct timeval tv;
      check(!gettimeofday(&tv, NULL));
      utmp->utmpx.ut_tv.tv_sec  = tv.tv_sec;
      utmp->utmpx.ut_tv.tv_usec = tv.tv_usec;

      // Temporarily regain privileges to update the utmp database
      uid_t r_uid, e_uid, s_uid;
      uid_t r_gid, e_gid, s_gid;
      check(!getresuid(&r_uid, &e_uid, &s_uid));
      check(!getresgid(&r_gid, &e_gid, &s_gid));
      setresuid(0, 0, 0);
      setresgid(0, 0, 0);

      setutxent();
      pututxline(&utmp->utmpx);
      endutxent();
      if (!utmp->useLogin) {
        updwtmpx("/var/log/wtmp", &utmp->utmpx);
      }
      
      // Switch back to the lower privileges
      check(!setresgid(r_gid, e_gid, s_gid));
      check(!setresuid(r_uid, e_uid, s_uid));

      NOINTR(close(utmp->pty));
    }
  }
}

void deleteUtmp(struct Utmp *utmp) {
  destroyUtmp(utmp);
  free(utmp);
}

static void destroyUtmpHashEntry(void *arg, char *key, char *value) {
  deleteUtmp((struct Utmp *)value);
}

void closeAllFds(int *exceptFds, int num) {
  // Close all file handles. If possible, scan through "/proc/self/fd" as
  // that is faster than calling close() on all possible file handles.
  int nullFd  = open("/dev/null", O_RDWR);
  int dirFd   = !&fdopendir ? -1 : open("/proc/self/fd", O_RDONLY);
  if (dirFd < 0) {
    for (int i = sysconf(_SC_OPEN_MAX); --i > 0; ) {
      if (i != nullFd) {
        for (int j = 0; j < num; j++) {
          if (i == exceptFds[j]) {
            goto no_close_1;
          }
        }
        // Closing handles 0..2 is never a good idea. Instead, redirect them
        // to /dev/null
        if (i <= 2) {
          NOINTR(dup2(nullFd, i));
        } else {
          NOINTR(close(i));
        }
      }
    no_close_1:;
    }
  } else {
    DIR *dir;
    check(dir = fdopendir(dirFd));
    struct dirent de, *res;
    while (!readdir_r(dir, &de, &res) && res) {
      if (res->d_name[0] < '0')
        continue;
      int fd  = atoi(res->d_name);
      if (fd != nullFd && fd != dirFd) {
        for (int j = 0; j < num; j++) {
          if (fd == exceptFds[j]) {
            goto no_close_2;
          }
        }
        // Closing handles 0..2 is never a good idea. Instead, redirect them
        // to /dev/null
        if (fd <= 2) {
          NOINTR(dup2(nullFd, fd));
        } else {
          NOINTR(close(fd));
        }
      }
    no_close_2:;
    }
    check(!closedir(dir));
  }
  if (nullFd > 2) {
    check(!close(nullFd));
  }
}

static int forkPty(int *pty, int useLogin, struct Utmp **utmp,
                   const char *peerName) {
  int slave;
  char ptyPath[PATH_MAX];
  if ((*pty               = getpt())                                < 0 ||
      grantpt(*pty)                                                 < 0 ||
      unlockpt(*pty)                                                < 0 ||
      ptsname_r(*pty, ptyPath, sizeof(ptyPath))                     < 0 ||
      (slave              = NOINTR(open(ptyPath, O_RDWR|O_NOCTTY))) < 0) {
    if (*pty >= 0) {
      NOINTR(close(*pty));
    }
    *pty                  = -1;
    *utmp                 = NULL;
    return -1;
  }

  // Fill in utmp entry
  *utmp                   = newUtmp(useLogin, ptyPath, peerName);

  // Now, fork off the child process
  pid_t pid;
  if ((pid                = fork()) < 0) {
    NOINTR(close(slave));
    NOINTR(close(*pty));
    *pty                  = -1;
    deleteUtmp(*utmp);
    *utmp                 = NULL;
    return -1;
  } else if (pid == 0) {
    pid                   = getpid();
    snprintf((char *)&(*utmp)->pid[0], sizeof((*utmp)->pid), "%d", pid);
    (*utmp)->utmpx.ut_pid = pid;
    (*utmp)->pty          = slave;

    closeAllFds((int []){ slave }, 1);

    // Become the session/process-group leader
    setsid();
    setpgid(0, 0);
    
    // Redirect standard I/O to the pty
    dup2(slave, 0);
    dup2(slave, 1);
    dup2(slave, 2);
    if (slave > 2) {
      NOINTR(close(slave));
    }
    *pty                  = 0;

    // Force the pty to be our control terminal
    NOINTR(close(NOINTR(open(ptyPath, O_RDWR))));

    return 0;
  } else {
    snprintf((char *)&(*utmp)->pid[0], sizeof((*utmp)->pid), "%d", pid);
    (*utmp)->utmpx.ut_pid = pid;
    (*utmp)->pty          = *pty;
    fcntl(*pty, F_SETFL, O_NONBLOCK|O_RDWR);
    NOINTR(close(slave));
    return pid;
  }
}

static const struct passwd *getPWEnt(uid_t uid) {
  struct passwd pwbuf, *pw;
  char *buf;
  int len                  = sysconf(_SC_GETPW_R_SIZE_MAX);
  check(len > 0);
  check(buf                = malloc(len));
  check(!getpwuid_r(uid, &pwbuf, buf, len, &pw) && pw);
  struct passwd *passwd;
  check(passwd             = malloc(sizeof(struct passwd) +
                                    strlen(pw->pw_name) +
                                    strlen(pw->pw_passwd) +
                                    strlen(pw->pw_gecos) +
                                    strlen(pw->pw_dir) +
                                    strlen(pw->pw_shell) + 5));
  passwd->pw_uid           = pw->pw_uid;
  passwd->pw_gid           = pw->pw_gid;
  strcpy(passwd->pw_shell  = strrchr(
  strcpy(passwd->pw_dir    = strrchr(
  strcpy(passwd->pw_gecos  = strrchr(
  strcpy(passwd->pw_passwd = strrchr(
  strcpy(passwd->pw_name   = (char *)(passwd + 1),
         pw->pw_name),   '\000') + 1,
         pw->pw_passwd), '\000') + 1,
         pw->pw_gecos),  '\000') + 1,
         pw->pw_dir),    '\000') + 1,
         pw->pw_shell);
  free(buf);
  return passwd;
}

static void sigAlrmHandler(int sig, siginfo_t *info, void *unused) {
  puts("\nLogin timed out after 60 seconds.");
  _exit(1);
}

static pam_handle_t *internalLogin(struct Service *service, struct Utmp *utmp,
                                   char ***environment) {
  // Time out after 60 seconds
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags                  = SA_SIGINFO;
  sa.sa_sigaction              = sigAlrmHandler;
  check(!sigaction(SIGALRM, &sa, NULL));
  alarm(60);

  // Use PAM to negotiate user authentication and authorization
  const struct passwd *pw;
  pam_handle_t *pam            = NULL;
  struct pam_conv conv         = { .conv = x_misc_conv };
  if (service->authUser) {
    check(supportsPAM());
    check(x_pam_start("shellinabox", NULL, &conv, &pam) == PAM_SUCCESS);

    // Change the prompt to include the host name
    struct utsname uts;
    check(!uname(&uts));
    const char *origPrompt;
    check(x_pam_get_item(pam, PAM_USER_PROMPT, (void *)&origPrompt) ==
          PAM_SUCCESS);
    char *prompt;
    check(prompt               = stringPrintf(NULL, "%s %s", uts.nodename,
                                         origPrompt ? origPrompt : "login: "));
    check(x_pam_set_item(pam, PAM_USER_PROMPT, prompt) == PAM_SUCCESS);

    // Up to three attempts to enter the user id and password
    for (int i = 0;;) {
      check(x_pam_set_item(pam, PAM_USER, NULL) == PAM_SUCCESS);
      int rc;
      if ((rc                  = x_pam_authenticate(pam, PAM_SILENT)) ==
          PAM_SUCCESS &&
          (geteuid() ||
          (rc                  = x_pam_acct_mgmt(pam, PAM_SILENT)) ==
           PAM_SUCCESS)) {
        break;
      }
      if (++i == 3) {
        // Quit if login failed.
        puts("\nMaximum number of tries exceeded (3)");
        x_pam_end(pam, rc);
        _exit(1);
      } else {
        puts("\nLogin incorrect");
      }
    }
    check(x_pam_set_item(pam, PAM_USER_PROMPT, "login: ") == PAM_SUCCESS);
    free(prompt);

    // Retrieve user id, and group id.
    const char *name;
    check(x_pam_get_item(pam, PAM_USER, (void *)&name) == PAM_SUCCESS);
    pw                         = getPWEnt(getUserId(name));
    check(service->uid < 0);
    check(service->gid < 0);
    check(!service->user);
    check(!service->group);
    service->uid               = pw->pw_uid;
    service->gid               = pw->pw_gid;
    check(service->user        = strdup(pw->pw_name));
    service->group             = getGroupName(pw->pw_gid);
  } else {
    check(service->uid >= 0);
    check(service->gid >= 0);
    check(service->user);
    check(service->group);
    if (supportsPAM()) {
      check(x_pam_start("shellinabox", service->user, &conv, &pam) ==
            PAM_SUCCESS);
      int rc;

      // PAM account management requires root access. Just skip it, if we
      // are running with lower privileges.
      if (geteuid() &&
          (rc                  = x_pam_acct_mgmt(pam, PAM_SILENT)) !=
          PAM_SUCCESS) {
        x_pam_end(pam, rc);
        _exit(1);
      }
    }
    pw                         = getPWEnt(service->uid);
  }

  if (restricted &&
      (service->uid != restricted || service->gid != pw->pw_gid)) {
    puts("\nAccess denied!");
    x_pam_end(pam, PAM_SUCCESS);
    _exit(1);
  }

  if (pam) {
    check(x_pam_set_item(pam, PAM_TTY, (const void **)utmp->utmpx.ut_line) ==
          PAM_SUCCESS);
  }

  // Retrieve supplementary group ids.
  int ngroups                  = 0;
  getgrouplist(service->user, pw->pw_gid, NULL, &ngroups);
  check(ngroups >= 0);
  if (ngroups > 0) {
    // Set supplementary group ids
    gid_t *groups;
    check(groups               = malloc((ngroups + 1) * sizeof(gid_t)));
    groups[ngroups]            = service->gid;
    check(getgrouplist(service->user, pw->pw_gid, groups, &ngroups) ==
          ngroups);

    // Make sure that any group that was requested on the command line is
    // included, if it is not one of the normal groups for this user.
    for (int i = 0; ; i++) {
      if (i == ngroups) {
        ngroups++;
        break;
      } else if (groups[i] == service->gid) {
        break;
      }
    }
    setgroups(ngroups, groups);
    free(groups);
  }

  // Add standard environment variables
  int numEnvVars               = 0;
  for (char **e = *environment; *e; numEnvVars++, e++) {
  }
  check(*environment           = realloc(*environment,
                                     (numEnvVars + 6)*sizeof(char *)));
  (*environment)[numEnvVars++] = stringPrintf(NULL, "HOME=%s", pw->pw_dir);
  (*environment)[numEnvVars++] = stringPrintf(NULL, "SHELL=%s", pw->pw_shell);
  check(
  (*environment)[numEnvVars++] = strdup(
                              "PATH=/usr/local/bin:/usr/bin:/bin:/usr/games"));
  (*environment)[numEnvVars++] = stringPrintf(NULL, "LOGNAME=%s",
                                              service->user);
  (*environment)[numEnvVars++] = stringPrintf(NULL, "USER=%s", service->user);
  (*environment)[numEnvVars++] = NULL;
  free((void *)pw);

  // Update utmp/wtmp entries
  memset(&utmp->utmpx.ut_user, 0, sizeof(utmp->utmpx.ut_user));
  strncat(&utmp->utmpx.ut_user[0], service->user, sizeof(utmp->utmpx.ut_user));
  setutxent();
  pututxline(&utmp->utmpx);
  endutxent();
  updwtmpx("/var/log/wtmp", &utmp->utmpx);

  alarm(0);
  return pam;
}

static void destroyVariableHashEntry(void *arg, char *key, char *value) {
  free(key);
  free(value);
}

static void execService(int width, int height, struct Service *service,
                        const char *peerName, char **environment) {
  // Create a hash table with all the variables that we can expand. This
  // includes all environment variables being passed to the child.
  HashMap *vars;
  check(vars                  = newHashMap(destroyVariableHashEntry, NULL));
  for (char **e = environment; *e; e++) {
    char *ptr                 = strchr(*e, '=');
    char *key, *value;
    if (!ptr) {
      check(key               = strdup(*e));
      check(value             = strdup(""));
    } else {
      check(key               = malloc(ptr - *e + 1));
      memcpy(key, *e, ptr - *e);
      key[ptr - *e]           = '\000';
      check(value             = strdup(ptr + 1));
    }
    // All of our variables are lower-case
    for (ptr = key; *ptr; ptr++) {
      if (*ptr >= 'A' && *ptr <= 'Z') {
        *ptr += 'a' - 'A';
      }
    }
    addToHashMap(vars, key, value);
  }
  char *key, *value;
  check(key                   = strdup("gid"));
  addToHashMap(vars, key, stringPrintf(NULL, "%d", service->gid));
  check(key                   = strdup("group"));
  check(value                 = strdup(service->group));
  addToHashMap(vars, key, value);
  check(key                   = strdup("peer"));
  check(value                 = strdup(peerName));
  addToHashMap(vars, key, value);
  check(key                   = strdup("uid"));
  addToHashMap(vars, key, stringPrintf(NULL, "%d", service->uid));

  enum { ENV, ARGS } state    = ENV;
  enum { NONE, SINGLE, DOUBLE
  } quote                     = NONE;
  char *cmdline;
  check(cmdline               = strdup(service->cmdline));
  int argc                    = 0;
  char **argv;
  check(argv                  = malloc(sizeof(char *)));
  key                         = NULL;
  value                       = NULL;
  for (char *ptr = cmdline; ; ptr++) {
    if (!key && *ptr && *ptr != ' ') {
      key                     = ptr;
    }
    switch (*ptr) {
    case '\'':
      if (quote == SINGLE || quote == NONE) {
        memmove(ptr, ptr + 1, strlen(ptr));
        ptr--;
        quote                 = quote == SINGLE ? NONE : SINGLE;
      } else {
        dcheck(quote == DOUBLE);
      }
      break;
    case '\"':
      if (quote == DOUBLE || quote == NONE) {
        memmove(ptr, ptr + 1, strlen(ptr));
        ptr--;
        quote                 = quote == DOUBLE ? NONE : DOUBLE;
      } else {
        dcheck(quote == SINGLE);
      }
      break;
    case '$':
      if ((quote == NONE || quote == DOUBLE) && ptr[1] == '{') {
        // Always treat environment variables as if they were quoted. There
        // is not good reason for us to try to look for spaces within
        // expanded environment variables. This just leads to subtle bugs.
        char *end             = ptr + 2;
        while (*end && *end != '}') {
          end++;
        }
        char ch               = *end;
        *end                  = '\000';
        const char *repl      = getFromHashMap(vars, ptr);
        int replLen           = repl ? strlen(repl) : 0;
        *end                  = ch;
        if (ch) {
          end++;
        }
        memmove(ptr + replLen, end, strlen(end) + 1);
        if (repl) {
          memcpy(ptr, repl, replLen);
        }
        ptr                  += replLen;
      }
      break;
    case '\\':
      if (!ptr[1]) {
        *ptr--                = '\000';
      } else {
        memmove(ptr, ptr + 1, strlen(ptr));
      }
      break;
    case '=':
      // This is the seperator between keys and values of any environment
      // variable that we are asked to set.
      if (state == ENV && quote == NONE && !value) {
        *ptr                  = '\000';
        value                 = ptr + 1;
      }
      break;
    case ' ':
      // If this space character is not quoted, this is the start of a new
      // command line argument.
      if (quote != NONE) {
        break;
      }
      // Fall thru
    case '\000':;
      char ch                 = *ptr;
      if (key) {
        *ptr                  = '\000';
        if (state == ENV && value) {
          // Override an existing environment variable.
          int numEnvVars = 0;
          int len             = strlen(key);
          for (char **e = environment; *e; e++, numEnvVars++) {
            if (!strncmp(*e, key, len) && (*e)[len] == '=') {
              check(*e        = realloc(*e, len + strlen(value) + 2));
              strcpy((*e) + len + 1, value);
              numEnvVars      = -1;
              break;
            }
          }
          // Add a new environment variable
          if (numEnvVars >= 0) {
            check(environment = realloc(environment,
                                        (numEnvVars + 2)*sizeof(char *)));
            value[-1]         = '=';
            environment[numEnvVars++] = strdup(key);
            environment[numEnvVars]   = NULL;
          }
        } else {
          // Add entry to argv.
          state               = ARGS;
          argv[argc++]        = key;
          check(argv          = realloc(argv, (argc + 1)*sizeof(char *)));
        }
      }
      key                     = NULL;
      value                   = NULL;
      if (!ch) {
        goto done;
      }
      break;
    default:
      break;
    }
  }
 done:
  argv[argc]                  = NULL;
  deleteHashMap(vars);
  check(argc);

  extern char **environ;
  environ                     = environment;
  char *cmd                   = strrchr(argv[0], '/');
  execvp(cmd ? cmd + 1: argv[0], argv);
}

void setWindowSize(int pty, int width, int height) {
  if (width > 0 && height > 0) {
    struct winsize win;
    win.ws_row    = height;
    win.ws_col    = width;
    win.ws_xpixel = 0;
    win.ws_ypixel = 0;
    ioctl(pty, TIOCSWINSZ, &win);
  }
}

static void childProcess(struct Service *service, int width, int height,
                         struct Utmp *utmp, const char *peerName) {
  // Set initial window size
  setWindowSize(0, width, height);

  // Set up environment variables
  static const char *legalEnv[] = { "TZ", "HZ", NULL };
  char **environment;
  check(environment             = malloc(2*sizeof(char *)));
  int numEnvVars                = 1;
  environment[0]                = "TERM=xterm";
  if (width > 0 && height > 0) {
    numEnvVars                 += 2;
    check(environment           = realloc(environment,
                                          (numEnvVars + 1)*sizeof(char *)));
    environment[numEnvVars-2]   = stringPrintf(NULL, "COLUMNS=%d", width);
    environment[numEnvVars-1]   = stringPrintf(NULL, "LINES=%d", height);
  }
  for (int i = 0; legalEnv[i]; i++) {
    char *value                 = getenv(legalEnv[i]);
    if (value) {
      numEnvVars++;
      check(environment         = realloc(environment,
                                          (numEnvVars + 1)*sizeof(char *)));
      environment[numEnvVars-1] = stringPrintf(NULL, "%s=%s",
                                               legalEnv[i], value);
    }
  }
  environment[numEnvVars]       = NULL;

  // Set initial terminal settings
  struct termios tt;
  tcgetattr(0, &tt);
  cfsetispeed(&tt, 38400);
  cfsetospeed(&tt, 38400);
  tt.c_iflag                    =  TTYDEF_IFLAG & ~ISTRIP;
  tt.c_oflag                    =  TTYDEF_OFLAG;
  tt.c_lflag                    =  TTYDEF_LFLAG;
  tt.c_cflag                    = (TTYDEF_CFLAG & ~(CS7|PARENB|HUPCL)) | CS8;
  tt.c_cc[VERASE]               = '\x7F';
  tcsetattr(0, TCSAFLUSH, &tt);

  // Assert root privileges in order to update utmp entry.
  setresuid(0, 0, 0);
  setresgid(0, 0, 0);
  setutxent();
  struct utmpx utmpx            = utmp->utmpx;
  if (service->useLogin || service->authUser) {
    utmpx.ut_type               = LOGIN_PROCESS;
    memset(utmpx.ut_host, 0, sizeof(utmpx.ut_host));
  }
  pututxline(&utmpx);
  endutxent();
  if (!utmp->useLogin) {
    memset(&utmpx.ut_user, 0, sizeof(utmpx.ut_user));
    strncat(&utmpx.ut_user[0], "LOGIN", sizeof(utmpx.ut_user));
    updwtmpx("/var/log/wtmp", &utmpx);
  }

  // Create session. We might have to fork another process as PAM wants us
  // to close the session when the child terminates. And we must retain
  // permissions, as session closure could require root permissions.
  // None of this really applies if we are running as an unprivileged user.
  // In that case, we do not bother about session management.
  if (!service->useLogin) {
    pam_handle_t *pam           = internalLogin(service, utmp, &environment);
    if (pam && !geteuid()) {
      check(x_pam_open_session(pam, PAM_SILENT) == PAM_SUCCESS);
      pid_t pid                 = fork();
      switch (pid) {
      case -1:
        _exit(1);
      case 0:
        break;
      default:;
        // Finish all pending PAM operations.
        int status, rc;
        check(waitpid(pid, &status, 0) == pid);
        check((rc               = x_pam_close_session(pam, PAM_SILENT)) ==
              PAM_SUCCESS);
        check(x_pam_end(pam, rc) == PAM_SUCCESS);
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
      }
    }
  }

  // Change user and group ids
  check(!setresgid(service->gid, service->gid, service->gid));
  check(!setresuid(service->uid, service->uid, service->uid));
  
  // Change working directory
  if (service->useHomeDir) {
    check(!service->useLogin);
    const struct passwd *pw     = getPWEnt(getuid());
    check(!service->cwd);
    check(service->cwd          = strdup(pw->pw_dir));
    free((void *)pw);
  }
  check(service->cwd);
  if (!*service->cwd || *service->cwd != '/' || chdir(service->cwd)) {
    check(service->cwd          = realloc((char *)service->cwd, 2));
    strcpy((char *)service->cwd, "/");
    puts("No directory, logging in with HOME=/");
    check(!chdir("/"));
    for (int i = 0; environment[i]; i++) {
      if (!strncmp(environment[i], "HOME=", 5)) {
        free(environment[i]);
        check(environment[i]    = strdup("HOME=/"));
        break;
      }
    }
  }

  // Finally, launch the child process.
  if (service->useLogin) {
    execle("/bin/login", "login", "-p", "-h", peerName, NULL, environment);
  } else {
    execService(width, height, service, peerName, environment);
  }
  _exit(1);
}

static void sigChildHandler(int sig, siginfo_t *info, void *unused) {
}

static void launcherDaemon(int fd) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags                 = SA_NOCLDSTOP | SA_SIGINFO;
  sa.sa_sigaction             = sigChildHandler;
  check(!sigaction(SIGCHLD, &sa, NULL));

  struct LaunchRequest request;
  for (;;) {
    errno                     = 0;
    int len                   = read(fd, &request, sizeof(request));
    if (len != sizeof(request) && errno != EINTR) {
      break;
    }

    // Check whether our read operation got interrupted, because a child
    // has died.
    int   status;
    pid_t pid;
    while (NOINTR(pid = waitpid(-1, &status, WNOHANG)) > 0) {
      if (WIFEXITED(pid) || WIFSIGNALED(pid)) {
        char key[32];
        snprintf(&key[0], sizeof(key), "%d", pid);
        deleteFromHashMap(childProcesses, key);
      }
    }
    if (len != sizeof(request)) {
      continue;
    }

    check(request.service >= 0);
    check(request.service < numServices);

    // Sanitize the host name, so that we do not pass any unexpected characters
    // to our child process.
    request.peerName[sizeof(request.peerName)-1] = '\000';
    for (char *s = request.peerName; *s; s++) {
      if (!((*s >= '0' && *s <= '9') ||
            (*s >= 'A' && *s <= 'Z') ||
            (*s >= 'a' && *s <= 'z') ||
             *s == '.' || *s == '-')) {
        *s                    = '-';
      }
    }

    // Fork and exec the child process.
    int pty;
    struct Utmp *utmp;
    if ((pid                  = forkPty(&pty,
                                        services[request.service]->useLogin,
                                        &utmp, request.peerName)) < 0) {
    } else if (pid == 0) {
      childProcess(services[request.service], request.width, request.height,
                   utmp, request.peerName);
      _exit(1);
    } else {
      // Remember the utmp entry so that we can clean up when the child
      // terminates.
      if (!childProcesses) {
        childProcesses        = newHashMap(destroyUtmpHashEntry, NULL);
      }
      addToHashMap(childProcesses, utmp->pid, (char *)utmp);

      // Send file handle and process id back to parent
      char cmsg_buf[CMSG_SPACE(sizeof(int))];
      struct iovec  iov       = { 0 };
      struct msghdr msg       = { 0 };
      iov.iov_base            = &pid;
      iov.iov_len             = sizeof(pid);
      msg.msg_iov             = &iov;
      msg.msg_iovlen          = 1;
      msg.msg_control         = &cmsg_buf;
      msg.msg_controllen      = sizeof(cmsg_buf);
      struct cmsghdr *cmsg    = CMSG_FIRSTHDR(&msg);
      check(cmsg);
      cmsg->cmsg_level        = SOL_SOCKET;
      cmsg->cmsg_type         = SCM_RIGHTS;
      cmsg->cmsg_len          = CMSG_LEN(sizeof(int));
      *(int *)CMSG_DATA(cmsg) = pty;
      if (NOINTR(sendmsg(fd, &msg, 0)) != sizeof(pid)) {
        break;
      }
      NOINTR(close(pty));
    }
  }
  deleteHashMap(childProcesses);
  _exit(0);
}

int forkLauncher(void) {
  int pair[2];
  check(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));

  switch (fork()) {
  case 0:;
    // If our real-uid is not "root", then we should not allow anybody to
    // login unauthenticated users as anyone other than their own.
    uid_t tmp;
    check(!getresuid(&restricted, &tmp, &tmp));

    // Temporarily drop most permissions. We still retain the ability to
    // switch back to root, which is necessary for launching "login".
    lowerPrivileges();
    NOINTR(close(pair[0]));
    closeAllFds((int []){ pair[1] }, 1);
    launcherDaemon(pair[1]);
    fatal("exit() failed!");
  case -1:
    fatal("fork() failed!");
  default:
    NOINTR(close(pair[1]));
    launcher = pair[0];
    return launcher;
  }
}

void terminateLauncher(void) {
  if (launcher >= 0) {
    NOINTR(close(launcher));
    launcher = -1;
  }
}
