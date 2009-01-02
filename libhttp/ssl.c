// ssl.c -- Support functions that find and load SSL support, if available
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

#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libhttp/ssl.h"
#include "libhttp/httpconnection.h"
#include "logging/logging.h"

#if defined(HAVE_PTHREAD_H)
// Pthread support is optional. Only enable it, if the library has been
// linked into the program
#include <pthread.h>
extern int pthread_once(pthread_once_t *, void (*)(void))__attribute__((weak));
extern int pthread_sigmask(int, const sigset_t *, sigset_t *)
                                                         __attribute__((weak));

#endif

// SSL support is optional. Only enable it, if the library can be loaded.
long          (*x_BIO_ctrl)(BIO *, int, long, void *);
BIO_METHOD *  (*x_BIO_f_buffer)(void);
void          (*x_BIO_free_all)(BIO *);
BIO *         (*x_BIO_new)(BIO_METHOD *);
BIO *         (*x_BIO_new_socket)(int, int);
BIO *         (*x_BIO_pop)(BIO *);
BIO *         (*x_BIO_push)(BIO *, BIO *);
void          (*x_ERR_clear_error)(void);
void          (*x_ERR_clear_error)(void);
unsigned long (*x_ERR_peek_error)(void);
unsigned long (*x_ERR_peek_error)(void);
long          (*x_SSL_CTX_callback_ctrl)(SSL_CTX *, int, void (*)(void));
int           (*x_SSL_CTX_check_private_key)(const SSL_CTX *);
long          (*x_SSL_CTX_ctrl)(SSL_CTX *, int, long, void *);
void          (*x_SSL_CTX_free)(SSL_CTX *);
SSL_CTX *     (*x_SSL_CTX_new)(SSL_METHOD *);
int           (*x_SSL_CTX_use_PrivateKey_file)(SSL_CTX *, const char *, int);
int           (*x_SSL_CTX_use_certificate_file)(SSL_CTX *, const char *, int);
long          (*x_SSL_ctrl)(SSL *, int, long, void *);
void          (*x_SSL_free)(SSL *);
int           (*x_SSL_get_error)(const SSL *, int);
void *        (*x_SSL_get_ex_data)(const SSL *, int);
BIO *         (*x_SSL_get_rbio)(const SSL *);
const char *  (*x_SSL_get_servername)(const SSL *, int);
BIO *         (*x_SSL_get_wbio)(const SSL *);
int           (*x_SSL_library_init)(void);
SSL *         (*x_SSL_new)(SSL_CTX *);
int           (*x_SSL_read)(SSL *, void *, int);
SSL_CTX *     (*x_SSL_set_SSL_CTX)(SSL *, SSL_CTX *);
void          (*x_SSL_set_accept_state)(SSL *);
void          (*x_SSL_set_bio)(SSL *, BIO *, BIO *);
int           (*x_SSL_set_ex_data)(SSL *, int, void *);
int           (*x_SSL_shutdown)(SSL *);
int           (*x_SSL_write)(SSL *, const void *, int);
SSL_METHOD *  (*x_SSLv23_server_method)(void);


static void sslDestroyCachedContext(void *ssl_, char *context_) {
  struct SSLSupport *ssl = (struct SSLSupport *)ssl_;
  SSL_CTX *context       = (SSL_CTX *)context_;
  if (context != ssl->sslContext) {
    SSL_CTX_free(context);
  }
}

struct SSLSupport *newSSL(void) {
  struct SSLSupport *ssl;
  check(ssl = malloc(sizeof(struct SSLSupport)));
  initSSL(ssl);
  return ssl;
}

void initSSL(struct SSLSupport *ssl) {
  ssl->enabled               = serverSupportsSSL();
  ssl->sslContext            = NULL;
  ssl->sniCertificatePattern = NULL;
  ssl->generateMissing       = 0;
  initTrie(&ssl->sniContexts, sslDestroyCachedContext, ssl);
}

void destroySSL(struct SSLSupport *ssl) {
  if (ssl) {
    free(ssl->sniCertificatePattern);
    destroyTrie(&ssl->sniContexts);
    if (ssl->sslContext) {
      dcheck(!ERR_peek_error());
      SSL_CTX_free(ssl->sslContext);
    }
  }
}

void deleteSSL(struct SSLSupport *ssl) {
  destroySSL(ssl);
  free(ssl);
}

#if defined(HAVE_OPENSSL)
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

static void loadSSL(void) {
  check(!SSL_library_init);
  struct {
    void       **var;
    const char *fn;
  } symbols[] = {
    { (void **)&x_BIO_ctrl,                    "BIO_ctrl" },
    { (void **)&x_BIO_f_buffer,                "BIO_f_buffer" },
    { (void **)&x_BIO_free_all,                "BIO_free_all" },
    { (void **)&x_BIO_new,                     "BIO_new" },
    { (void **)&x_BIO_new_socket,              "BIO_new_socket" },
    { (void **)&x_BIO_pop,                     "BIO_pop" },
    { (void **)&x_BIO_push,                    "BIO_push" },
    { (void **)&x_ERR_clear_error,             "ERR_clear_error" },
    { (void **)&x_ERR_clear_error,             "ERR_clear_error" },
    { (void **)&x_ERR_peek_error,              "ERR_peek_error" },
    { (void **)&x_ERR_peek_error,              "ERR_peek_error" },
    { (void **)&x_SSL_CTX_callback_ctrl,       "SSL_CTX_callback_ctrl" },
    { (void **)&x_SSL_CTX_check_private_key,   "SSL_CTX_check_private_key" },
    { (void **)&x_SSL_CTX_ctrl,                "SSL_CTX_ctrl" },
    { (void **)&x_SSL_CTX_free,                "SSL_CTX_free" },
    { (void **)&x_SSL_CTX_new,                 "SSL_CTX_new" },
    { (void **)&x_SSL_CTX_use_PrivateKey_file, "SSL_CTX_use_PrivateKey_file" },
    { (void **)&x_SSL_CTX_use_certificate_file,"SSL_CTX_use_certificate_file"},
    { (void **)&x_SSL_ctrl,                    "SSL_ctrl" },
    { (void **)&x_SSL_free,                    "SSL_free" },
    { (void **)&x_SSL_get_error,               "SSL_get_error" },
    { (void **)&x_SSL_get_ex_data,             "SSL_get_ex_data" },
    { (void **)&x_SSL_get_rbio,                "SSL_get_rbio" },
#ifdef TLSEXT_NAMETYPE_host_name
    { (void **)&x_SSL_get_servername,          "SSL_get_servername" },
#endif
    { (void **)&x_SSL_get_wbio,                "SSL_get_wbio" },
    { (void **)&x_SSL_library_init,            "SSL_library_init" },
    { (void **)&x_SSL_new,                     "SSL_new" },
    { (void **)&x_SSL_read,                    "SSL_read" },
#ifdef TLSEXT_NAMETYPE_host_name
    { (void **)&x_SSL_set_SSL_CTX,             "SSL_set_SSL_CTX" },
#endif
    { (void **)&x_SSL_set_accept_state,        "SSL_set_accept_state" },
    { (void **)&x_SSL_set_bio,                 "SSL_set_bio" },
    { (void **)&x_SSL_set_ex_data,             "SSL_set_ex_data" },
    { (void **)&x_SSL_shutdown,                "SSL_shutdown" },
    { (void **)&x_SSL_write,                   "SSL_write" },
    { (void **)&x_SSLv23_server_method,        "SSLv23_server_method" }
  };
  for (int i = 0; i < sizeof(symbols)/sizeof(symbols[0]); i++) {
    if (!(*symbols[i].var = loadSymbol("libssl.so", symbols[i].fn))) {
      debug("Failed to load SSL support. Could not find \"%s\"",
            symbols[i].fn);
      for (int j = 0; j < sizeof(symbols)/sizeof(symbols[0]); j++) {
        *symbols[j].var = NULL;
      }
      return;
    }
  }
  SSL_library_init();
  dcheck(!ERR_peek_error());
  debug("Loaded SSL suppport");
}
#endif

int serverSupportsSSL(void) {
#if defined(HAVE_OPENSSL)
  // We want to call loadSSL() exactly once. For single-threaded applications,
  // this is straight-forward. For threaded applications, we need to call
  // pthread_once(), instead. We perform run-time checks for whether we are
  // single- or multi-threaded, so that the same code can be used.
#if defined(HAVE_PTHREAD_H)
  if (!!&pthread_once) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, loadSSL);
  } else
#endif
  {
    static int initialized;
    if (!initialized) {
      initialized = 1;
      loadSSL();
    }
  }
  return !!SSL_library_init;
#else
  return 0;
#endif
}

void sslGenerateCertificate(const char *certificate, const char *serverName) {
 debug("Auto-generating missing certificate \"%s\" for \"%s\"",
       certificate, serverName);
  char *cmd         = stringPrintf(NULL,
    "set -e; "
    "exec 2>/dev/null </dev/null; "
    "umask 0377; "
    "PATH=/usr/bin "
    "openssl req -x509 -nodes -days 7300 -newkey rsa:1024 -keyout /dev/stdout "
                                 "-out /dev/stdout -subj '/CN=%s/' | cat>'%s'",
    serverName, certificate);
  if (system(cmd)) {
    warn("Failed to generate self-signed certificate \"%s\"", certificate);
  }
  free(cmd);
}

#ifdef TLSEXT_NAMETYPE_host_name
static int sslSNICallback(SSL *sslHndl, int *al, struct SSLSupport *ssl) {
  check(!ERR_peek_error());
  const char *name        = SSL_get_servername(sslHndl,
                                               TLSEXT_NAMETYPE_host_name);
  if (name == NULL || !*name) {
    return SSL_TLSEXT_ERR_OK;
  }
  struct HttpConnection *http =
                            (struct HttpConnection *)SSL_get_app_data(sslHndl);
  debug("Received SNI callback for virtual host \"%s\" from \"%s:%d\"",
        name, httpGetPeerName(http), httpGetPort(http));
  char *serverName;
  check(serverName        = malloc(strlen(name)+2));
  serverName[0]           = '-';
  for (int i = 0;;) {
    char ch               = name[i];
    if (ch >= 'A' && ch <= 'Z') {
      ch                 |= 0x20;
    } else if (ch != '\000' && ch != '.' && ch != '-' &&
               (ch < '0' ||(ch > '9' && ch < 'A') || (ch > 'Z' &&
                ch < 'a')|| ch > 'z')) {
      i++;
      continue;
    }
    serverName[++i]       = ch;
    if (!ch) {
      break;
    }
  }
  if (!*serverName) {
    free(serverName);
    return SSL_TLSEXT_ERR_OK;
  }
  SSL_CTX *context        = (SSL_CTX *)getFromTrie(&ssl->sniContexts,
                                                   serverName+1,
                                                   NULL);
  if (context == NULL) {
    check(context         = SSL_CTX_new(SSLv23_server_method()));
    check(ssl->sniCertificatePattern);
    char *certificate     = stringPrintf(NULL, ssl->sniCertificatePattern,
                                         serverName);
    if (!SSL_CTX_use_certificate_file(context, certificate, SSL_FILETYPE_PEM)||
        !SSL_CTX_use_PrivateKey_file(context, certificate, SSL_FILETYPE_PEM) ||
        !SSL_CTX_check_private_key(context)) {
      if (ssl->generateMissing) {
        sslGenerateCertificate(certificate, serverName + 1);
        if (!SSL_CTX_use_certificate_file(context, certificate,
                                          SSL_FILETYPE_PEM) ||
            !SSL_CTX_use_PrivateKey_file(context, certificate,
                                         SSL_FILETYPE_PEM) ||
            !SSL_CTX_check_private_key(context)) {
          goto certificate_missing;
        }
      } else {
      certificate_missing:
        warn("Could not find matching certificate \"%s\" for \"%s\"",
             certificate, serverName + 1);
        SSL_CTX_free(context);
        context           = ssl->sslContext;
      }
    }
    ERR_clear_error();
    free(certificate);
    addToTrie(&ssl->sniContexts, serverName+1, (char *)context);
  }
  free(serverName);
  if (context != ssl->sslContext) {
    check(SSL_set_SSL_CTX(sslHndl, context) > 0);
  }
  check(!ERR_peek_error());
  return SSL_TLSEXT_ERR_OK;
}
#endif

void sslSetCertificate(struct SSLSupport *ssl, const char *filename,
                       int autoGenerateMissing) {
#if defined(HAVE_OPENSSL)
  check(serverSupportsSSL());

  char *defaultCertificate;
  check(defaultCertificate           = strdup(filename));
  char *ptr                          = strchr(defaultCertificate, '%');
  if (ptr != NULL) {
    check(!strchr(ptr+1, '%'));
    check(ptr[1] == 's');
    memmove(ptr, ptr + 2, strlen(ptr)-1);
  }

  check(ssl->sslContext              = SSL_CTX_new(SSLv23_server_method()));
  if (autoGenerateMissing) {
    if (!SSL_CTX_use_certificate_file(ssl->sslContext, defaultCertificate,
                                      SSL_FILETYPE_PEM) ||
        !SSL_CTX_use_PrivateKey_file(ssl->sslContext, defaultCertificate,
                                     SSL_FILETYPE_PEM) ||
        !SSL_CTX_check_private_key(ssl->sslContext)) {
      char hostname[256], buf[4096];
      check(!gethostname(hostname, sizeof(hostname)));
      struct hostent he_buf, *he;
      int h_err;
      if (gethostbyname_r(hostname, &he_buf, buf, sizeof(buf),
                          &he, &h_err)) {
        sslGenerateCertificate(defaultCertificate, hostname);
      } else {
        sslGenerateCertificate(defaultCertificate, he->h_name);
      }
    } else {
      goto valid_certificate;
    }
  }
  if (!SSL_CTX_use_certificate_file(ssl->sslContext, defaultCertificate,
                                    SSL_FILETYPE_PEM) ||
      !SSL_CTX_use_PrivateKey_file(ssl->sslContext, defaultCertificate,
                                   SSL_FILETYPE_PEM) ||
      !SSL_CTX_check_private_key(ssl->sslContext)) {
    fatal("Cannot read valid certificate from \"%s\". "
          "Check file permissions and file format.", defaultCertificate);
  }
 valid_certificate:
  free(defaultCertificate);

#ifdef TLSEXT_NAMETYPE_host_name
  if (ptr != NULL) {
    check(ssl->sniCertificatePattern = strdup(filename));
    check(SSL_CTX_set_tlsext_servername_callback(ssl->sslContext,
                                                 sslSNICallback));
    check(SSL_CTX_set_tlsext_servername_arg(ssl->sslContext, ssl));
  }
#endif
  dcheck(!ERR_peek_error());
  ERR_clear_error();

  ssl->generateMissing               = autoGenerateMissing;
#endif
}

int sslEnable(struct SSLSupport *ssl, int enabled) {
  int old      = ssl->enabled;
  ssl->enabled = enabled;
  return old;
}

void sslBlockSigPipe(void) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
  dcheck(!(&pthread_sigmask ? pthread_sigmask : sigprocmask)
                                                      (SIG_BLOCK, &set, NULL));
}

int sslUnblockSigPipe(void) {
  int signum = 0;
  sigset_t set;
  check(!sigpending(&set));
  if (sigismember(&set, SIGPIPE)) {
    sigwait(&set, &signum);
  }
  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
  check(!(&pthread_sigmask ? pthread_sigmask : sigprocmask)
                                                    (SIG_UNBLOCK, &set, NULL));
  return signum;
}

int sslPromoteToSSL(struct SSLSupport *ssl, SSL **sslHndl, int fd,
                    const char *buf, int len) {
#if defined(HAVE_OPENSSL)
  sslBlockSigPipe();
  int rc          = 0;
  check(!*sslHndl);
  dcheck(!ERR_peek_error());
  dcheck(*sslHndl = SSL_new(ssl->sslContext));
  if (*sslHndl == NULL) {
    ERR_clear_error();
    errno         = EINVAL;
    rc            = -1;
  } else {
    SSL_set_mode(*sslHndl, SSL_MODE_ENABLE_PARTIAL_WRITE);
    BIO *writeBIO = BIO_new_socket(fd, 0);
    BIO *readBIO  = writeBIO;
    if (len > 0) {
      readBIO     = BIO_new(BIO_f_buffer());
      BIO_push(readBIO, writeBIO);
      check(BIO_set_buffer_read_data(readBIO, (char *)buf, len));
    }
    SSL_set_bio(*sslHndl, readBIO, writeBIO);
    SSL_set_accept_state(*sslHndl);
    dcheck(!ERR_peek_error());
  }
  sslUnblockSigPipe();
  return rc;
#else
  errno           = EINVAL;
  return -1;
#endif
}

void sslFreeHndl(SSL **sslHndl) {
#if defined(HAVE_OPENSSL)
  if (*sslHndl) {
    // OpenSSL does not always correctly perform reference counting for stacked
    // BIOs. This is particularly a problem if an SSL connection has two
    // different BIOs for the read and the write end, with one being a stacked
    // derivative of the other. Unfortunately, this is exactly the scenario
    // that we set up.
    // As a work-around, we un-stack the BIOs prior to freeing the SSL
    // connection.
    ERR_clear_error();
    BIO *writeBIO, *readBIO;
    check(writeBIO    = SSL_get_wbio(*sslHndl));
    check(readBIO     = SSL_get_rbio(*sslHndl));
    if (writeBIO != readBIO) {
      if (readBIO->next_bio == writeBIO) {
        // OK, that's exactly the bug we are looking for. We know how to
        // fix it.
        check(BIO_pop(readBIO) == writeBIO);
        check(readBIO->references == 1);
        check(writeBIO->references == 1);
        check(!readBIO->next_bio);
        check(!writeBIO->prev_bio);
      } else if (readBIO->next_bio == writeBIO->next_bio &&
                 writeBIO->next_bio->prev_bio == writeBIO) {
        // Things get even more confused, if the SSL handshake is aborted
        // prematurely.
        // OpenSSL appears to internally stack a BIO onto the read end that
        // does not get removed afterwards. We end up with the original
        // socket BIO having two different BIOs prepended to it (one for
        // reading and one for writing). In this case, not only is the
        // reference count wrong, but the chain of next_bio/prev_bio pairs
        // is corrupted, too.
        BIO *sockBIO;
        check(sockBIO = BIO_pop(readBIO));
        check(sockBIO == BIO_pop(writeBIO));
        check(readBIO->references == 1);
        check(writeBIO->references == 1);
        check(sockBIO->references == 1);
        check(!readBIO->next_bio);
        check(!writeBIO->next_bio);
        check(!sockBIO->prev_bio);
        BIO_free_all(sockBIO);
      } else {
        // We do not know, how to fix this situation. Something must have
        // changed in the OpenSSL internals. Either, this is a new bug, or
        // somebody fixed the code in a way that we did not anticipate.
        fatal("Unexpected corruption of OpenSSL data structures");
      }
    }
    SSL_free(*sslHndl);
    dcheck(!ERR_peek_error());
  }
#endif
  *sslHndl            = NULL;
}
