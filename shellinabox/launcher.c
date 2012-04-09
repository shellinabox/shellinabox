// launcher.c -- Launch services from a privileged process
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

#define pthread_once    x_pthread_once
#define execle          x_execle

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
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

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#ifdef HAVE_PTY_H
#include <pty.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif

#ifdef HAVE_UTMPX_H
#include <utmpx.h>
#endif

#if defined(HAVE_SECURITY_PAM_APPL_H)
#include <security/pam_appl.h>

#if defined(HAVE_SECURITY_PAM_MISC_H)
#include <security/pam_misc.h>
#endif

#ifndef PAM_DATA_SILENT
#define PAM_DATA_SILENT 0
#endif
#else
struct pam_message;
struct pam_response;
struct pam_conv;
typedef struct pam_handle pam_handle_t;
#endif

#ifdef HAVE_STRLCAT
#define strncat(a,b,c) ({ char *_a = (a); strlcat(_a, (b), (c)+1); _a; })
#endif

#include "shellinabox/launcher.h"
#include "shellinabox/privileges.h"
#include "shellinabox/service.h"
#include "libhttp/hashmap.h"
#include "logging/logging.h"

#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

#undef pthread_once
#undef execle
int execle(const char *, const char *, ...);

#if defined(HAVE_PTHREAD_H) && defined(__linux__)
#include <pthread.h>
extern int pthread_once(pthread_once_t *, void (*)(void))__attribute__((weak));
#endif

// If PAM support is available, take advantage of it. Otherwise, silently fall
// back on legacy operations for session management.
#if defined(HAVE_SECURITY_PAM_APPL_H) && defined(HAVE_DLOPEN)
static int (*x_pam_acct_mgmt)(pam_handle_t *, int);
static int (*x_pam_authenticate)(pam_handle_t *, int);
#if defined(HAVE_SECURITY_PAM_CLIENT_H)
static int (**x_pam_binary_handler_fn)(void *, pamc_bp_t *);
#endif
static int (*x_pam_close_session)(pam_handle_t *, int);
static int (*x_pam_end)(pam_handle_t *, int);
static int (*x_pam_get_item)(const pam_handle_t *, int, const void **);
static int (*x_pam_open_session)(pam_handle_t *, int);
static int (*x_pam_set_item)(pam_handle_t *, int, const void *);
static int (*x_pam_start)(const char *, const char *, const struct pam_conv *,
                          pam_handle_t **);
static int (*x_misc_conv)(int, const struct pam_message **,
                          struct pam_response **, void *);

#define pam_acct_mgmt         x_pam_acct_mgmt
#define pam_authenticate      x_pam_authenticate
#define pam_binary_handler_fn x_pam_binary_handler_fn
#define pam_close_session     x_pam_close_session
#define pam_end               x_pam_end
#define pam_get_item          x_pam_get_item
#define pam_open_session      x_pam_open_session
#define pam_set_item          x_pam_set_item
#define pam_start             x_pam_start
#define misc_conv             x_misc_conv
#endif

static int   launcher = -1;
static uid_t restricted;

// MacOS X has a somewhat unusual definition of getgrouplist() which can
// trigger a compile warning.
#if defined(HAVE_GETGROUPLIST_TAKES_INTS)
static int x_getgrouplist(const char *user, gid_t group,
                          gid_t *groups, int *ngroups) {
  return getgrouplist(user, (int)group, (int *)groups, ngroups);
}
#define getgrouplist x_getgrouplist
#endif

// BSD systems have special requirements on how utmp entries have to be filled
// out in order to be updated by non-privileged users. In particular, they
// want the real user name in the utmp recode.
// This all wouldn't be so bad, if pututxline() wouldn't print an error message
// to stderr, if it fails to run. Unfortunately, it has been observed to do so.
// That means, we need to jump through some hoops to intercept these messages.
#ifdef HAVE_UTMPX_H
struct utmpx *x_pututxline(struct utmpx *ut) {
  // N.B. changing global file descriptors isn't thread safe. But all call
  // sites are guaranteed to be single-threaded. If that ever changes, this
  // code will need rewriting.
  int oldStdin         = dup(0);
  int oldStdout        = dup(1);
  int oldStderr        = dup(2);
  check(oldStdin > 2 && oldStdout > 2 && oldStderr > 2);
  int nullFd           = open("/dev/null", O_RDWR);
  check(nullFd > 2);
  check(dup2(nullFd, 0) == 0);
  NOINTR(close(nullFd));

  // Set up a pipe so that we can read error messages that might be printed
  // to stderr. We assume that the kernel maintains a buffer that is
  // sufficiently large to receive the bytes written to it without causing
  // the I/O operation to block.
  int fds[2];
  check(!pipe(fds));
  check(dup2(fds[1], 1) == 1);
  check(dup2(fds[1], 2) == 2);
  NOINTR(close(fds[1]));
  struct utmpx *ret    = pututxline(ut);
  int err              = ret == NULL;

  // Close the write end of the pipe, so that we can read until EOF.
  check(dup2(0, 1) == 1);
  check(dup2(0, 2) == 2);
  char buf[128];
  while (NOINTR(read(fds[0], buf, sizeof(buf))) > 0) {
    err                = 1;
  }
  NOINTR(close(fds[0]));

  // If we either received an error from pututxline() or if we saw an error
  // message being written out, adjust the utmp record and retry.
  if (err) {
    uid_t uid          = getuid();
    if (uid) {
      // We only retry if the code is not running as root. Otherwise, fixing
      // the utmp record is unlikely to do anything for us.
      // If running as non-root, we set the actual user name in the utmp
      // record. This is not ideal, but if it allows us to update the record
      // then that's the best we do.
      const char *user = getUserName(uid);
      if (user) {
        memset(&ut->ut_user[0], 0, sizeof(ut->ut_user));
        strncat(&ut->ut_user[0], user, sizeof(ut->ut_user) - 1);
        ret            = pututxline(ut);
        free((char *)user);
      }
    }
  }

  // Clean up. Reset file descriptors back to their original values.
  check(dup2(oldStderr, 2) == 2);
  check(dup2(oldStdout, 1) == 1);
  check(dup2(oldStdin,  0) == 0);
  NOINTR(close(oldStdin));
  NOINTR(close(oldStdout));
  NOINTR(close(oldStderr));

  // It is quite likely that we won't always be in a situation to update the
  // system's utmp records. Return a non-fatal error to the caller.

  return ret;
}
#define pututxline x_pututxline
#endif

// If the PAM misc library cannot be found, we have to provide our own basic
// conversation function. As we know that this code is only ever called from
// ShellInABox, it can be kept significantly simpler than the more generic
// code that the PAM library implements.

static int read_string(int echo, const char *prompt, char **retstr) {
  *retstr                = NULL;
  struct termios term_before, term_tmp;
  if (tcgetattr(0, &term_before) != 0) {
    return -1;
  }
  memcpy(&term_tmp, &term_before, sizeof(term_tmp));
  if (!echo) {
    term_tmp.c_lflag    &= ~ECHO;
  }
  int nc;
  for (;;) {
    tcsetattr(0, TCSAFLUSH, &term_tmp);
    fprintf(stderr, "%s", prompt);
    char *line;
    const int lineLength = 512;
    check(line           = calloc(1, lineLength));
    nc                   = read(0, line, lineLength - 1);
    tcsetattr(0, TCSADRAIN, &term_before);
    if (!echo) {
      fprintf(stderr, "\n");
    }
    if (nc > 0) {
      if (line[nc-1] == '\n') {
        nc--;
      } else if (echo) {
        fprintf(stderr, "\n");
      }
      line[nc]           = '\000';
      check(*retstr      = line);
      break;
    } else {
      memset(line, 0, lineLength);
      free(line);
      if (echo) {
        fprintf(stderr, "\n");
      }
      break;
    }
  }
  tcsetattr(0, TCSADRAIN, &term_before);
  return nc;
}

#if defined(HAVE_SECURITY_PAM_APPL_H) && defined(HAVE_DLOPEN)
#if defined(HAVE_SECURITY_PAM_CLIENT_H)
static pamc_bp_t *p(pamc_bp_t *p) {
  // GCC is too smart for its own good, and triggers a warning in
  // PAM_BP_RENEW, unless we pass the first argument through a function.
  return p;
}
#endif

static int my_misc_conv(int num_msg, const struct pam_message **msgm,
                        struct pam_response **response, void *appdata_ptr) {
  if (num_msg <= 0) {
    return PAM_CONV_ERR;
  }
  struct pam_response *reply;
  check(reply = (struct pam_response *)calloc(num_msg,
                                              sizeof(struct pam_response)));
  for (int count = 0; count < num_msg; count++) {
    char *string                 = NULL;
    switch(msgm[count]->msg_style) {
      case PAM_PROMPT_ECHO_OFF:
        if (read_string(0, msgm[count]->msg, &string) < 0) {
          goto failed_conversation;
        }
        break;
      case PAM_PROMPT_ECHO_ON:
        if (read_string(1, msgm[count]->msg, &string) < 0) {
          goto failed_conversation;
        }
        break;
      case PAM_ERROR_MSG:
        if (fprintf(stderr, "%s\n", msgm[count]->msg) < 0) {
          goto failed_conversation;
        }
        break;
      case PAM_TEXT_INFO:
        if (fprintf(stdout, "%s\n", msgm[count]->msg) < 0) {
          goto failed_conversation;
        }
        break;
#if defined(HAVE_SECURITY_PAM_CLIENT_H)
      case PAM_BINARY_PROMPT: {
        pamc_bp_t binary_prompt = NULL;
        if (!msgm[count]->msg || !*pam_binary_handler_fn) {
          goto failed_conversation;
        }
        PAM_BP_RENEW(p(&binary_prompt), PAM_BP_RCONTROL(msgm[count]->msg),
                     PAM_BP_LENGTH(msgm[count]->msg));
        PAM_BP_FILL(binary_prompt, 0, PAM_BP_LENGTH(msgm[count]->msg),
                    PAM_BP_RDATA(msgm[count]->msg));
        if ((*pam_binary_handler_fn)(appdata_ptr, &binary_prompt) !=
            PAM_SUCCESS || !binary_prompt) {
          goto failed_conversation;
        }
        string                  = (char *)binary_prompt;
        break; }
#endif
      default:
        goto failed_conversation;
    }
    if (string) {
      reply[count].resp_retcode = 0;
      reply[count].resp         = string;
    }
  }
failed_conversation:
  *response                     = reply;
  return PAM_SUCCESS;
}

static void *loadSymbol(const char *lib, const char *fn) {
  void *dl = RTLD_DEFAULT;
  void *rc = dlsym(dl, fn);
  if (!rc) {
#ifdef RTLD_NOLOAD
    dl     = dlopen(lib, RTLD_LAZY|RTLD_GLOBAL|RTLD_NOLOAD);
#else
    dl     = NULL;
#endif
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
  check(!pam_start);
  check(!misc_conv);
  struct {
    union {
      void     *avoid_gcc_warning_about_type_punning;
      void     **var;
    };
    const char *lib;
    const char *fn;
  } symbols[] = {
    { { &pam_acct_mgmt },         "libpam.so",      "pam_acct_mgmt"     },
    { { &pam_authenticate },      "libpam.so",      "pam_authenticate"  },
#if defined(HAVE_SECURITY_PAM_CLIENT_H)
    { { &pam_binary_handler_fn }, "libpam_misc.so", "pam_binary_handler_fn" },
#endif
    { { &pam_close_session },     "libpam.so",      "pam_close_session" },
    { { &pam_end },               "libpam.so",      "pam_end"           },
    { { &pam_get_item },          "libpam.so",      "pam_get_item"      },
    { { &pam_open_session },      "libpam.so",      "pam_open_session"  },
    { { &pam_set_item },          "libpam.so",      "pam_set_item"      },
    { { &pam_start },             "libpam.so",      "pam_start"         },
    { { &misc_conv },             "libpam_misc.so", "misc_conv"         }
  };
  for (unsigned i = 0; i < sizeof(symbols)/sizeof(symbols[0]); i++) {
    if (!(*symbols[i].var = loadSymbol(symbols[i].lib, symbols[i].fn))) {
#if defined(HAVE_SECURITY_PAM_CLIENT_H)
      if (!strcmp(symbols[i].fn, "pam_binary_handler_fn")) {
        // Binary conversation support is optional
        continue;
      } else
#endif
      if (!strcmp(symbols[i].fn, "misc_conv")) {
        // PAM misc is optional
        *symbols[i].var = (void *)my_misc_conv;
        continue;
      }
      debug("Failed to load PAM support. Could not find \"%s\"",
            symbols[i].fn);
      for (unsigned j = 0; j < sizeof(symbols)/sizeof(symbols[0]); j++) {
        *symbols[j].var = NULL;
      }
      return;
    }
  }
  debug("Loaded PAM suppport");
}
#endif

int supportsPAM(void) {
#if defined(HAVE_SECURITY_PAM_APPL_H) && !defined(HAVE_DLOPEN)
  return 1;
#else
#if defined(HAVE_SECURITY_PAM_APPL_H)

  // We want to call loadPAM() exactly once. For single-threaded applications,
  // this is straight-forward. For threaded applications, we need to call
  // pthread_once(), instead. We perform run-time checks for whether we are
  // single- or multi-threaded, so that the same code can be used.
  // This currently only works on Linux.
#if defined(HAVE_PTHREAD_H) && defined(__linux__) && defined(__i386__)
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
  return misc_conv && pam_start;
#else
  return 0;
#endif
#endif
}

#ifndef HAVE_GETPWUID_R
// This is a not-thread-safe replacement for getpwuid_r()
#define getpwuid_r x_getpwuid_r
static int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
                      struct passwd **result) {
  if (result) {
    *result        = NULL;
  }
  if (!pwd) {
    return -1;
  }
  errno            = 0;
  struct passwd *p = getpwuid(uid);
  if (!p) {
    return errno ? -1 : 0;
  }
  *pwd             = *p;
  if (result) {
    *result        = pwd;
  }
  return 0;
}
#endif

int launchChild(int service, struct Session *session, const char *url) {
  if (launcher < 0) {
    errno              = EINVAL;
    return -1;
  }

  char *u;
  check(u              = strdup(url));
  for (int i; u[i = strcspn(u, "\\\"'`${};() \r\n\t\v\f")]; ) {
    static const char hex[] = "0123456789ABCDEF";
    check(u            = realloc(u, strlen(u) + 4));
    memmove(u + i + 3, u + i + 1, strlen(u + i));
    u[i + 2]           = hex[ u[i]       & 0xF];
    u[i + 1]           = hex[(u[i] >> 4) & 0xF];
    u[i]               = '%';
  }

  struct LaunchRequest *request;
  ssize_t len          = sizeof(struct LaunchRequest) + strlen(u) + 1;
  check(request        = calloc(len, 1));
  request->service     = service;
  request->width       = session->width;
  request->height      = session->height;
  strncat(request->peerName, httpGetPeerName(session->http),
          sizeof(request->peerName) - 1);
  request->urlLength   = strlen(u);
  memcpy(&request->url, u, request->urlLength);
  free(u);
  if (NOINTR(write(launcher, request, len)) != len) {
    free(request);
    return -1;
  }
  free(request);
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
  memcpy(&session->pty, CMSG_DATA(cmsg), sizeof(int));
  return pid;
}

struct Utmp {
  const char   pid[32];
  int          pty;
  int          useLogin;
#ifdef HAVE_UTMPX_H
  struct utmpx utmpx;
#endif
};

static HashMap *childProcesses;

void initUtmp(struct Utmp *utmp, int useLogin, const char *ptyPath,
              const char *peerName) {
  memset(utmp, 0, sizeof(struct Utmp));
  utmp->pty                 = -1;
  utmp->useLogin            = useLogin;
#ifdef HAVE_UTMPX_H
  utmp->utmpx.ut_type       = useLogin ? LOGIN_PROCESS : USER_PROCESS;
  dcheck(!strncmp(ptyPath, "/dev/pts", 8) ||
         !strncmp(ptyPath, "/dev/pty", 8) ||
         !strncmp(ptyPath, "/dev/tty", 8));
  strncat(&utmp->utmpx.ut_line[0], ptyPath + 5,   sizeof(utmp->utmpx.ut_line) - 1);
  strncat(&utmp->utmpx.ut_id[0],   ptyPath + 8,   sizeof(utmp->utmpx.ut_id) - 1);
  strncat(&utmp->utmpx.ut_user[0], "SHELLINABOX", sizeof(utmp->utmpx.ut_user) - 1);
  strncat(&utmp->utmpx.ut_host[0], peerName,      sizeof(utmp->utmpx.ut_host) - 1);
  struct timeval tv;
  check(!gettimeofday(&tv, NULL));
  utmp->utmpx.ut_tv.tv_sec  = tv.tv_sec;
  utmp->utmpx.ut_tv.tv_usec = tv.tv_usec;
#endif
}

struct Utmp *newUtmp(int useLogin, const char *ptyPath,
                     const char *peerName) {
  struct Utmp *utmp;
  check(utmp = malloc(sizeof(struct Utmp)));
  initUtmp(utmp, useLogin, ptyPath, peerName);
  return utmp;
}

#if defined(HAVE_UPDWTMP) && !defined(HAVE_UPDWTMPX)
#define min(a,b) ({ typeof(a) _a=(a); typeof(b) _b=(b); _a < _b ? _a : _b; })
#define updwtmpx x_updwtmpx

static void updwtmpx(const char *wtmpx_file, const struct utmpx *utx) {
  struct utmp ut   = { 0 };
  ut.ut_type       = utx->ut_type;
  ut.ut_pid        = utx->ut_pid;
  ut.ut_tv.tv_sec  = utx->ut_tv.tv_sec;
  ut.ut_tv.tv_usec = utx->ut_tv.tv_usec;
  memcpy(&ut.ut_line, &utx->ut_line,
         min(sizeof(ut.ut_line), sizeof(utx->ut_line)));
  memcpy(&ut.ut_id, &utx->ut_id,
         min(sizeof(ut.ut_id), sizeof(utx->ut_id)));
  memcpy(&ut.ut_user, &utx->ut_user,
         min(sizeof(ut.ut_user), sizeof(utx->ut_user)));
  memcpy(&ut.ut_host, &utx->ut_host,
         min(sizeof(ut.ut_host), sizeof(utx->ut_host)));
  updwtmp(wtmpx_file, &ut);
}
#endif

void destroyUtmp(struct Utmp *utmp) {
  if (utmp) {
    if (utmp->pty >= 0) {
#ifdef HAVE_UTMPX_H
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

#if defined(HAVE_UPDWTMP) || defined(HAVE_UPDWTMPX)
      if (!utmp->useLogin) {
        updwtmpx("/var/log/wtmp", &utmp->utmpx);
      }
#endif
      
      // Switch back to the lower privileges
      check(!setresgid(r_gid, e_gid, s_gid));
      check(!setresuid(r_uid, e_uid, s_uid));
#endif

      NOINTR(close(utmp->pty));
    }
  }
}

void deleteUtmp(struct Utmp *utmp) {
  destroyUtmp(utmp);
  free(utmp);
}

static void destroyUtmpHashEntry(void *arg ATTR_UNUSED, char *key ATTR_UNUSED,
                                 char *value) {
  UNUSED(arg);
  UNUSED(key);
  deleteUtmp((struct Utmp *)value);
}

void closeAllFds(int *exceptFds, int num) {
  // Close all file handles. If possible, scan through "/proc/self/fd" as
  // that is faster than calling close() on all possible file handles.
  int nullFd  = open("/dev/null", O_RDWR);
  DIR *dir    = opendir("/proc/self/fd");
  if (dir == 0) {
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
    struct dirent de, *res;
    while (!readdir_r(dir, &de, &res) && res) {
      if (res->d_name[0] < '0')
        continue;
      int fd  = atoi(res->d_name);
      if (fd != nullFd && fd != dirfd(dir)) {
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

#if !defined(HAVE_PTSNAME_R)
static int ptsname_r(int fd, char *buf, size_t buflen) {
  // It is unfortunate that ptsname_r is not universally available.
  // For the time being, this is not a big problem, as ShellInABox is
  // single-threaded (and so is the launcher process). But if this
  // code gets re-used in a multi-threaded application, that could
  // lead to problems.
  if (buf == NULL) {
    errno = EINVAL;
    return -1;
  }
  char *p = ptsname(fd);
  if (p == NULL) {
    return -1;
  }
  if (buflen < strlen(p) + 1) {
    errno = ERANGE;
    return -1;
  }
  strcpy(buf, p);
  return 0;
}
#endif

static int forkPty(int *pty, int useLogin, struct Utmp **utmp,
                   const char *peerName) {
  int slave;
  #ifdef HAVE_OPENPTY
  char* ptyPath = NULL;
  if (openpty(pty, &slave, NULL, NULL, NULL) < 0) {
    *pty                    = -1;
    *utmp                   = NULL;
    return -1;
  }
  // Recover name of PTY in a Hurd compatible way.  PATH_MAX doesn't
  // exist, so we need to use ttyname_r to fetch the name of the
  // pseudo-tty and find the buffer length that is sufficient. After
  // finding an adequate buffer size for the ptyPath, we allocate it
  // on the stack and release the freestore copy.  In this was we know
  // that the memory won't leak.  Note that ptsname_r is not always
  // available but we're already checking for this so it will be
  // defined in any case.
  {
    size_t length = 32;
    char* path = NULL;
    while (path == NULL) {
      path = malloc (length);
      *path = 0;
      if (ptsname_r (*pty, path, length)) {
        if (errno == ERANGE) {
          free (path);
          path = NULL;
        }
        else
          break;          // Every other error means no name for us
      }
      length <<= 1;
    }
    length = strlen (path);
    ptyPath = alloca (length + 1);
    strcpy (ptyPath, path);
    free (path);
  }
  #else
  char ptyPath[PATH_MAX];
  if ((*pty                 = posix_openpt(O_RDWR|O_NOCTTY))          < 0 ||
      grantpt(*pty)                                                   < 0 ||
      unlockpt(*pty)                                                  < 0 ||
      ptsname_r(*pty, ptyPath, sizeof(ptyPath))                       < 0 ||
      (slave                = NOINTR(open(ptyPath, O_RDWR|O_NOCTTY))) < 0) {
    if (*pty >= 0) {
      NOINTR(close(*pty));
    }

    // Try old-style pty handling
    char fname[40]          = "/dev/ptyXX";
    for (const char *ptr1   = "pqrstuvwxyzabcde"; *ptr1; ptr1++) {
      fname[8]              = *ptr1;
      for (const char *ptr2 = "0123456789abcdef"; *ptr2; ptr2++) {
        fname[9]            = *ptr2;
        if ((*pty           = NOINTR(open(fname, O_RDWR, 0))) < 0) {
          if (errno == ENOENT) {
            continue;
          }
        }
        grantpt(*pty);
        unlockpt(*pty);
        if (ptsname_r(*pty, ptyPath, sizeof(ptyPath)) < 0) {
          strcpy(ptyPath, fname);
          ptyPath[5]        = 't';
        }
        if ((slave          = NOINTR(open(ptyPath, O_RDWR|O_NOCTTY))) >= 0) {
          debug("Opened old-style pty: %s", ptyPath);
          goto success;
        }
        NOINTR(close(*pty));
      }
    }
    *pty                    = -1;
    *utmp                   = NULL;
    return -1;
  }
 success:
  #endif

  // Fill in utmp entry
  *utmp                     = newUtmp(useLogin, ptyPath, peerName);

  // Now, fork off the child process
  pid_t pid;
  if ((pid                  = fork()) < 0) {
    NOINTR(close(slave));
    NOINTR(close(*pty));
    *pty                    = -1;
    deleteUtmp(*utmp);
    *utmp                   = NULL;
    return -1;
  } else if (pid == 0) {
    pid                     = getpid();
    snprintf((char *)&(*utmp)->pid[0], sizeof((*utmp)->pid), "%d", pid);
#ifdef HAVE_UTMPX_H
    (*utmp)->utmpx.ut_pid   = pid;
#endif
    (*utmp)->pty            = slave;

    closeAllFds((int []){ slave }, 1);

#ifdef HAVE_LOGIN_TTY
    login_tty(slave);
#else
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
#endif
    *pty                    = 0;

    // Force the pty to be our control terminal
    NOINTR(close(NOINTR(open(ptyPath, O_RDWR))));

    return 0;
  } else {
    snprintf((char *)&(*utmp)->pid[0], sizeof((*utmp)->pid), "%d", pid);
#ifdef HAVE_UTMPX_H
    (*utmp)->utmpx.ut_pid   = pid;
#endif
    (*utmp)->pty            = *pty;
    fcntl(*pty, F_SETFL, O_NONBLOCK|O_RDWR);
    NOINTR(close(slave));
    return pid;
  }
}

static const struct passwd *getPWEnt(uid_t uid) {
  struct passwd pwbuf, *pw;
  char *buf;
  #ifdef _SC_GETPW_R_SIZE_MAX
  int len                           = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (len <= 0) {
    len                             = 4096;
  }
  #else
  int len                           = 4096;
  #endif
  check(buf                         = malloc(len));
  check(!getpwuid_r(uid, &pwbuf, buf, len, &pw) && pw);
  if (!pw->pw_name  ) pw->pw_name   = (char *)"";
  if (!pw->pw_passwd) pw->pw_passwd = (char *)"";
  if (!pw->pw_gecos ) pw->pw_gecos  = (char *)"";
  if (!pw->pw_dir   ) pw->pw_dir    = (char *)"";
  if (!pw->pw_shell ) pw->pw_shell  = (char *)"";
  struct passwd *passwd;
  check(passwd                      = calloc(sizeof(struct passwd) +
                                             strlen(pw->pw_name) +
                                             strlen(pw->pw_passwd) +
                                             strlen(pw->pw_gecos) +
                                             strlen(pw->pw_dir) +
                                             strlen(pw->pw_shell) + 5, 1));
  passwd->pw_uid                    = pw->pw_uid;
  passwd->pw_gid                    = pw->pw_gid;
  strncat(passwd->pw_shell          = strrchr(
  strncat(passwd->pw_dir            = strrchr(
  strncat(passwd->pw_gecos          = strrchr(
  strncat(passwd->pw_passwd         = strrchr(
  strncat(passwd->pw_name           = (char *)(passwd + 1),
         pw->pw_name,   strlen(pw->pw_name)),   '\000') + 1,
         pw->pw_passwd, strlen(pw->pw_passwd)), '\000') + 1,
         pw->pw_gecos,  strlen(pw->pw_gecos)),  '\000') + 1,
         pw->pw_dir,    strlen(pw->pw_dir)),    '\000') + 1,
         pw->pw_shell,  strlen(pw->pw_shell));
  free(buf);
  return passwd;
}

static void sigAlrmHandler(int sig ATTR_UNUSED, siginfo_t *info ATTR_UNUSED,
                           void *unused ATTR_UNUSED) {
  UNUSED(sig);
  UNUSED(info);
  UNUSED(unused);
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

  // Change the prompt to include the host name
  const char *hostname         = NULL;
  if (service->authUser == 2 /* SSH */) {
    // If connecting to a remote host, include that hostname
    hostname                   = strrchr(service->cmdline, '@');
    if (!hostname || !strcmp(++hostname, "localhost")) {
      hostname                 = NULL;
    }
  }
  struct utsname uts;
  memset(&uts, 0, sizeof(uts));
  if (!hostname) {
    // Find our local hostname
    check(!uname(&uts));
    hostname                   = uts.nodename;
  }
  const char *fqdn;
  check(fqdn                   = strdup(hostname));
  check(hostname               = strdup(hostname));
  char *dot                    = strchr(hostname, '.');
  if (dot) {
    *dot                       = '\000';
  }

  const struct passwd *pw;
  pam_handle_t *pam            = NULL;
  if (service->authUser == 2 /* SSH */) {
    // Just ask for the user name. SSH will negotiate the password
    char *user                 = NULL;
    char *prompt;
    check(prompt               = stringPrintf(NULL, "%s login: ", hostname));
    for (;;) {
      if (read_string(1, prompt, &user) <= 0) {
        free(user);
        free(prompt);
        _exit(1);
      }
      if (*user) {
        for (char *u = user; *u; u++) {
          char ch              = *u;
          if (!((ch >= '0' && ch <= '9') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= 'a' && ch <= 'z') ||
                ch == '-' || ch == '_' || ch == '.')) {
            goto invalid_user_name;
          }
        }
        break;
      }
    invalid_user_name:
      free(user);
      user                     = NULL;
    }
    free(prompt);
    char *cmdline              = stringPrintf(NULL, service->cmdline, user);
    free(user);

    // Replace '@localhost' with the actual host name. This results in a nicer
    // prompt when SSH asks for the password.
    char *ptr                  = strrchr(cmdline, '@');
    if (!strcmp(ptr + 1, "localhost")) {
      int offset               = ptr + 1 - cmdline;
      check(cmdline            = realloc(cmdline,
                                         strlen(cmdline) + strlen(fqdn) -
                                         strlen("localhost") + 1));
      ptr                      = cmdline + offset;
      *ptr                     = '\000';
      strncat(ptr, fqdn, strlen(fqdn));
    }

    free((void *)service->cmdline);
    service->cmdline           = cmdline;

    // Run SSH as an unprivileged user
    if ((service->uid          = restricted) == 0) {
      if (runAsUser >= 0) {
        service->uid           = runAsUser;
      } else {
        service->uid           = getUserId("nobody");
      }
      if (runAsGroup >= 0) {
        service->gid           = runAsGroup;
      } else {
        service->gid           = getGroupId("nogroup");
      }
    }
    pw                         = getPWEnt(service->uid);
    if (restricted) {
      service->gid             = pw->pw_gid;
    }
    service->user              = getUserName(service->uid);
    service->group             = getGroupName(service->gid);
  } else {
    // Use PAM to negotiate user authentication and authorization
#if defined(HAVE_SECURITY_PAM_APPL_H)
    struct pam_conv conv       = { .conv = misc_conv };
    if (service->authUser) {
      check(supportsPAM());
      check(pam_start("shellinabox", NULL, &conv, &pam) == PAM_SUCCESS);

      const char *origPrompt;
      check(pam_get_item(pam, PAM_USER_PROMPT, (void *)&origPrompt) ==
            PAM_SUCCESS);
      char *prompt;
      check(prompt             = stringPrintf(NULL, "%s %s", hostname,
                                         origPrompt ? origPrompt : "login: "));
      check(pam_set_item(pam, PAM_USER_PROMPT, prompt) == PAM_SUCCESS);

      // Up to three attempts to enter the user id and password
      for (int i = 0;;) {
        check(pam_set_item(pam, PAM_USER, NULL) == PAM_SUCCESS);
        int rc;
        if ((rc                = pam_authenticate(pam, PAM_SILENT)) ==
            PAM_SUCCESS &&
            (geteuid() ||
             (rc               = pam_acct_mgmt(pam, PAM_SILENT)) ==
             PAM_SUCCESS)) {
          break;
        }
        if (++i == 3) {
          // Quit if login failed.
          puts("\nMaximum number of tries exceeded (3)");
          pam_end(pam, rc);
          _exit(1);
        } else {
          puts("\nLogin incorrect");
        }
      }
      check(pam_set_item(pam, PAM_USER_PROMPT, "login: ") == PAM_SUCCESS);
      free(prompt);

      // Retrieve user id, and group id.
      const char *name;
      check(pam_get_item(pam, PAM_USER, (void *)&name) == PAM_SUCCESS);
      pw                       = getPWEnt(getUserId(name));
      check(service->uid < 0);
      check(service->gid < 0);
      check(!service->user);
      check(!service->group);
      service->uid             = pw->pw_uid;
      service->gid             = pw->pw_gid;
      check(service->user      = strdup(pw->pw_name));
      service->group           = getGroupName(pw->pw_gid);
    } else {
      check(service->uid >= 0);
      check(service->gid >= 0);
      check(service->user);
      check(service->group);
      if (supportsPAM()) {
        check(pam_start("shellinabox", service->user, &conv, &pam) ==
              PAM_SUCCESS);
        int rc;

        // PAM account management requires root access. Just skip it, if we
        // are running with lower privileges.
        if (!geteuid() &&
            (rc                = pam_acct_mgmt(pam, PAM_SILENT)) !=
            PAM_SUCCESS) {
          pam_end(pam, rc);
          _exit(1);
        }
      }
      pw                       = getPWEnt(service->uid);
    }
#else
    check(!supportsPAM());
    pw                         = getPWEnt(service->uid);
#endif
  }
  free((void *)fqdn);
  free((void *)hostname);

  if (service->useDefaultShell) {
    check(!service->cmdline);
    service->cmdline           = strdup(*pw->pw_shell ?
                                        pw->pw_shell : "/bin/sh");
  }

  if (restricted &&
      (service->uid != (int)restricted || service->gid != (int)pw->pw_gid)) {
    puts("\nAccess denied!");
#if defined(HAVE_SECURITY_PAM_APPL_H)
    if (service->authUser != 2 /* SSH */) {
      pam_end(pam, PAM_SUCCESS);
    }
#endif
    _exit(1);
  }

  if (service->authUser != 2 /* SSH */) {
#if defined(HAVE_SECURITY_PAM_APPL_H)
    if (pam) {
#ifdef HAVE_UTMPX_H
      check(pam_set_item(pam, PAM_TTY, (const void **)utmp->utmpx.ut_line) ==
            PAM_SUCCESS);
#endif
    }
#else
    check(!pam);
#endif
  }

  // Retrieve supplementary group ids.
  int ngroups;
#if defined(__linux__)
  // On Linux, we can query the number of supplementary groups. On all other
  // platforms, we play it safe and just assume a fixed upper bound.
  ngroups                      = 0;
  getgrouplist(service->user, pw->pw_gid, NULL, &ngroups);
#else
  ngroups                      = 128;
#endif
  check(ngroups >= 0);
  if (ngroups > 0) {
    // Set supplementary group ids
    gid_t *groups;
    check(groups               = malloc((ngroups + 1) * sizeof(gid_t)));
    check(getgrouplist(service->user, pw->pw_gid, groups, &ngroups) >= 0);

    // Make sure that any group that was requested on the command line is
    // included, if it is not one of the normal groups for this user.
    for (int i = 0; ; i++) {
      if (i == ngroups) {
        groups[ngroups++]      = service->gid;
        break;
      } else if ((int)groups[i] == service->gid) {
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
#ifdef HAVE_UTMPX_H
  if (service->authUser != 2 /* SSH */) {
    memset(&utmp->utmpx.ut_user, 0, sizeof(utmp->utmpx.ut_user));
    strncat(&utmp->utmpx.ut_user[0], service->user,
            sizeof(utmp->utmpx.ut_user) - 1);
    setutxent();
    pututxline(&utmp->utmpx);
    endutxent();

#if defined(HAVE_UPDWTMP) || defined(HAVE_UPDWTMPX)
    updwtmpx("/var/log/wtmp", &utmp->utmpx);
#endif
  }
#endif

  alarm(0);
  return pam;
}

static void destroyVariableHashEntry(void *arg ATTR_UNUSED, char *key,
                                     char *value) {
  UNUSED(arg);
  free(key);
  free(value);
}

static void execService(int width ATTR_UNUSED, int height ATTR_UNUSED,
                        struct Service *service, const char *peerName,
                        char **environment, const char *url) {
  UNUSED(width);
  UNUSED(height);

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
  check(key                   = strdup("url"));
  addToHashMap(vars, key, strdup(url));

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
        // is no good reason for us to try to look for spaces within
        // expanded environment variables. This just leads to subtle bugs.
        char *end             = ptr + 2;
        while (*end && *end != '}') {
          end++;
        }
        char ch               = *end;
        *end                  = '\000';
        const char *repl      = getFromHashMap(vars, ptr + 2);
        int replLen           = repl ? strlen(repl) : 0;
        *end                  = ch;
        if (ch) {
          end++;
        }
        int incr              = replLen - (end - ptr);
        if (incr > 0) {
          char *oldCmdline    = cmdline;
          check(cmdline       = realloc(cmdline,
                                        (end - cmdline) + strlen(end) +
                                        incr + 1));
          ptr                += cmdline - oldCmdline;
          end                += cmdline - oldCmdline;
          if (key) {
            key              += cmdline - oldCmdline;
          }
          if (value) {
            value            += cmdline - oldCmdline;
          }
        }
        memmove(ptr + replLen, end, strlen(end) + 1);
        if (repl) {
          memcpy(ptr, repl, replLen);
        }
        ptr                  += replLen - 1;
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
              int s_size      = len + strlen(value) + 1;
              check(*e        = realloc(*e, s_size + 1));
              (*e)[len + 1]   = '\000';
              strncat(*e, value, s_size);
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
          argv[argc++]        = strdup(key);
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
  free(cmdline);
  argv[argc]                  = NULL;
  deleteHashMap(vars);
  check(argc);

  extern char **environ;
  environ                     = environment;
  char *cmd                   = strdup(argv[0]);
  char *slash                 = strrchr(argv[0], '/');
  if (slash) {
    memmove(argv[0], slash + 1, strlen(slash));
  }
  if (service->useDefaultShell) {
    int len                   = strlen(argv[0]);
    check(argv[0]             = realloc(argv[0], len + 2));
    memmove(argv[0] + 1, argv[0], len);
    argv[0][0]                = '-';
    argv[0][len + 1]          = '\000';
  }
  execvp(cmd, argv);
}

void setWindowSize(int pty, int width, int height) {
  if (width > 0 && height > 0) {
    #ifdef TIOCSSIZE
    {
      struct ttysize win;
      ioctl(pty, TIOCGSIZE, &win);
      win.ts_lines = height;
      win.ts_cols  = width;
      ioctl(pty, TIOCSSIZE, &win);
    }
    #endif
    #ifdef TIOCGWINSZ
    {
      struct winsize win;
      ioctl(pty, TIOCGWINSZ, &win);
      win.ws_row   = height;
      win.ws_col   = width;
      ioctl(pty, TIOCSWINSZ, &win);
    }
    #endif
  }
}

static void childProcess(struct Service *service, int width, int height,
                         struct Utmp *utmp, const char *peerName,
                         const char *url) {
  // Set initial window size
  setWindowSize(0, width, height);

  // Set up environment variables
  static const char *legalEnv[] = { "TZ", "HZ", NULL };
  char **environment;
  check(environment             = malloc(2*sizeof(char *)));
  int numEnvVars                = 1;
  check(environment[0]          = strdup("TERM=xterm"));
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
  struct termios tt = { 0 };
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
#ifdef HAVE_UTMPX_H
  setutxent();
  struct utmpx utmpx            = utmp->utmpx;
  if (service->useLogin || service->authUser) {
    utmpx.ut_type               = LOGIN_PROCESS;
    memset(utmpx.ut_host, 0, sizeof(utmpx.ut_host));
  }
  pututxline(&utmpx);
  endutxent();

#if defined(HAVE_UPDWTMP) || defined(HAVE_UPDWTMPX)
  if (!utmp->useLogin) {
    memset(&utmpx.ut_user, 0, sizeof(utmpx.ut_user));
    strncat(&utmpx.ut_user[0], "LOGIN", sizeof(utmpx.ut_user) - 1);
    updwtmpx("/var/log/wtmp", &utmpx);
  }
#endif
#endif

  // Create session. We might have to fork another process as PAM wants us
  // to close the session when the child terminates. And we must retain
  // permissions, as session closure could require root permissions.
  // None of this really applies if we are running as an unprivileged user.
  // In that case, we do not bother about session management.
  if (!service->useLogin) {
    pam_handle_t *pam           = internalLogin(service, utmp, &environment);
#if defined(HAVE_SECURITY_PAM_APPL_H)
    if (pam && !geteuid()) {
      if (pam_open_session(pam, PAM_SILENT) != PAM_SUCCESS) {
        fprintf(stderr, "Access denied.\n");
        _exit(1);
      }
      pid_t pid                 = fork();
      switch (pid) {
      case -1:
        _exit(1);
      case 0:
        break;
      default:;
        // Finish all pending PAM operations.
        int status, rc;
        check(NOINTR(waitpid(pid, &status, 0)) == pid);
        rc = pam_close_session(pam, PAM_SILENT);
        pam_end(pam, rc | PAM_DATA_SILENT);
        _exit(WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status));
      }
    }
#else
    check(!pam);
#endif
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
    *(char *)service->cwd       = '\000';
    strncat((char *)service->cwd, "/", 1);
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
  if (service->useLogin == 1) {
    execle("/bin/login", "login", "-p", "-h", peerName,
           (void *)0, environment);
    execle("/usr/bin/login", "login", "-p", "-h", peerName,
           (void *)0, environment);
  } else {
    execService(width, height, service, peerName, environment, url);
  }
  _exit(1);
}

static void sigChildHandler(int sig ATTR_UNUSED, siginfo_t *info ATTR_UNUSED,
                            void *unused ATTR_UNUSED) {
  UNUSED(sig);
  UNUSED(info);
  UNUSED(unused);
}

static void launcherDaemon(int fd) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags                 = SA_NOCLDSTOP | SA_SIGINFO;
  sa.sa_sigaction             = sigChildHandler;
  check(!sigaction(SIGCHLD, &sa, NULL));

  // pututxline() can cause spurious SIGHUP signals. Better ignore those.
  signal(SIGHUP, SIG_IGN);

  struct LaunchRequest request;
  for (;;) {
    errno                     = 0;
    int len                   = read(fd, &request, sizeof(request));
    if (len != sizeof(request) && errno != EINTR) {
      if (len) {
        debug("Failed to read launch request");
      }
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

    char *url;
    check(url                 = calloc(request.urlLength + 1, 1));
  readURL:
    len                       = read(fd, url, request.urlLength + 1);
    if (len != request.urlLength + 1 && errno != EINTR) {
      debug("Failed to read URL");
      free(url);
      break;
    }
    while (NOINTR(pid = waitpid(-1, &status, WNOHANG)) > 0) {
      if (WIFEXITED(pid) || WIFSIGNALED(pid)) {
        char key[32];
        snprintf(&key[0], sizeof(key), "%d", pid);
        deleteFromHashMap(childProcesses, key);
      }
    }
    if (len != request.urlLength + 1) {
      goto readURL;
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
                                        &utmp, request.peerName)) == 0) {
      childProcess(services[request.service], request.width, request.height,
                   utmp, request.peerName, url);
      free(url);
      _exit(1);
    } else {
      // Remember the utmp entry so that we can clean up when the child
      // terminates.
      free(url);
      if (pid > 0) {
        if (!childProcesses) {
          childProcesses      = newHashMap(destroyUtmpHashEntry, NULL);
        }
        addToHashMap(childProcesses, utmp->pid, (char *)utmp);
      } else {
        int fds[2];
        if (!pipe(fds)) {
          NOINTR(write(fds[1], "forkpty() failed\r\n", 18));
          NOINTR(close(fds[1]));
          pty                 = fds[0];
          pid                 = 0;
        }
      }

      // Send file handle and process id back to parent
      char cmsg_buf[CMSG_SPACE(sizeof(int))]; // = { 0 }; // Valid initializer makes OSX mad.
      memset (cmsg_buf, 0, sizeof (cmsg_buf)); // Quiet complaint from valgrind
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
      memcpy(CMSG_DATA(cmsg), &pty, sizeof(int));
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
    closeAllFds((int []){ pair[1], 2 }, 2);
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
