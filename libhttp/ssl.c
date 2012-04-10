// ssl.c -- Support functions that find and load SSL support, if available
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
#define pthread_sigmask x_pthread_sigmask

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libhttp/ssl.h"
#include "libhttp/httpconnection.h"
#include "logging/logging.h"

#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

#undef pthread_once
#undef pthread_sigmask

#if defined(HAVE_OPENSSL) && !defined(OPENSSL_NO_TLSEXT) &&                   \
    defined(TLSEXT_NAMETYPE_host_name) && defined(SSL_TLSEXT_ERR_OK)
#define HAVE_TLSEXT
#endif

#if defined(HAVE_PTHREAD_H)
// Pthread support is optional. Only enable it, if the library has been
// linked into the program
#include <pthread.h>
#if defined(__linux__)
extern int pthread_once(pthread_once_t *, void (*)(void))__attribute__((weak));
#endif
extern int pthread_sigmask(int, const sigset_t *, sigset_t *)
                                                         __attribute__((weak));

#endif

#if defined(HAVE_DLOPEN)
// SSL support is optional. Only enable it, if the library can be loaded.
long          (*BIO_ctrl)(BIO *, int, long, void *);
BIO_METHOD *  (*BIO_f_buffer)(void);
void          (*BIO_free_all)(BIO *);
BIO *         (*BIO_new)(BIO_METHOD *);
BIO *         (*BIO_new_socket)(int, int);
BIO *         (*BIO_pop)(BIO *);
BIO *         (*BIO_push)(BIO *, BIO *);
void          (*ERR_clear_error)(void);
void          (*ERR_clear_error)(void);
unsigned long (*ERR_peek_error)(void);
unsigned long (*ERR_peek_error)(void);
long          (*SSL_CTX_callback_ctrl)(SSL_CTX *, int, void (*)(void));
int           (*SSL_CTX_check_private_key)(const SSL_CTX *);
long          (*SSL_CTX_ctrl)(SSL_CTX *, int, long, void *);
void          (*SSL_CTX_free)(SSL_CTX *);
SSL_CTX *     (*SSL_CTX_new)(SSL_METHOD *);
int           (*SSL_CTX_use_PrivateKey_file)(SSL_CTX *, const char *, int);
int           (*SSL_CTX_use_PrivateKey_ASN1)(int, SSL_CTX *,
                                             const unsigned char *, long);
int           (*SSL_CTX_use_certificate_file)(SSL_CTX *, const char *, int);
int           (*SSL_CTX_use_certificate_ASN1)(SSL_CTX *, long,
                                              const unsigned char *);
long          (*SSL_ctrl)(SSL *, int, long, void *);
void          (*SSL_free)(SSL *);
int           (*SSL_get_error)(const SSL *, int);
void *        (*SSL_get_ex_data)(const SSL *, int);
BIO *         (*SSL_get_rbio)(const SSL *);
const char *  (*SSL_get_servername)(const SSL *, int);
BIO *         (*SSL_get_wbio)(const SSL *);
int           (*SSL_library_init)(void);
SSL *         (*SSL_new)(SSL_CTX *);
int           (*SSL_read)(SSL *, void *, int);
SSL_CTX *     (*SSL_set_SSL_CTX)(SSL *, SSL_CTX *);
void          (*SSL_set_accept_state)(SSL *);
void          (*SSL_set_bio)(SSL *, BIO *, BIO *);
int           (*SSL_set_ex_data)(SSL *, int, void *);
int           (*SSL_shutdown)(SSL *);
int           (*SSL_write)(SSL *, const void *, int);
SSL_METHOD *  (*SSLv23_server_method)(void);
X509 *        (*d2i_X509)(X509 **px, const unsigned char **in, int len);
void          (*X509_free)(X509 *a);
#endif

static void sslDestroyCachedContext(void *ssl_, char *context_) {
  struct SSLSupport *ssl = (struct SSLSupport *)ssl_;
  SSL_CTX *context       = (SSL_CTX *)context_;
#if defined(HAVE_OPENSSL)
  if (context != ssl->sslContext) {
    SSL_CTX_free(context);
  }
#else
  check(!context);
  check(!ssl->sslContext);
#endif
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
#if defined(HAVE_OPENSSL)
    if (ssl->sslContext) {
      dcheck(!ERR_peek_error());
      SSL_CTX_free(ssl->sslContext);
    }
#else
    check(!ssl->sslContext);
#endif
  }
}

void deleteSSL(struct SSLSupport *ssl) {
  destroySSL(ssl);
  free(ssl);
}

#if defined(HAVE_OPENSSL) && defined(HAVE_DLOPEN)
static int maybeLoadCrypto(void) {
  // Some operating systems cannot automatically load dependent dynamic
  // libraries. As libssl.so can depend on libcrypto.so, we try to load
  // it, iff we haven't tried loading it before and iff libssl.so does not
  // work by itself.
  static int crypto;
  // SHELLINABOX_LIBCRYPTO_SO can be used to select the specific
  // soname of libcrypto for systems where it is not libcrypto.so.
  // The feature is currently disabled.
  const char* path_libcrypto = NULL; // getenv ("SHELLINABOX_LIBCRYPTO_SO");
  if (path_libcrypto == NULL)
    path_libcrypto = "libcrypto.so";

  if (!crypto++) {
#ifdef RTLD_NOLOAD
    if (dlopen(path_libcrypto, RTLD_LAZY|RTLD_GLOBAL|RTLD_NOLOAD))
      return 1;
    else
#endif
      if (dlopen(path_libcrypto, RTLD_LAZY|RTLD_GLOBAL))
        return 1;
  }
  return 0;
}

static void *loadSymbol(const char *lib, const char *fn) {
  int err  = NOINTR(dup(2));
  if (err > 2) {
    int null = NOINTR(open("/dev/null", O_WRONLY));
    if (null >= 0) {
      NOINTR(dup2(null, 2));
      NOINTR(close(null));
    }
  }
  void *dl = RTLD_DEFAULT;
  void *rc = dlsym(dl, fn);
  if (!rc) {
    for (int i = 0; i < 2; i++) {
#ifdef RTLD_NOLOAD
      dl   = dlopen(lib, RTLD_LAZY|RTLD_GLOBAL|RTLD_NOLOAD);
#else
      dl   = NULL;
#endif
      if (dl == NULL) {
        dl = dlopen(lib, RTLD_LAZY|RTLD_GLOBAL);
      }
      if (dl != NULL || !maybeLoadCrypto()) {
        break;
      }
    }
    if (dl != NULL) {
      rc   = dlsym(RTLD_DEFAULT, fn);
      if (rc == NULL && maybeLoadCrypto()) {
        rc = dlsym(RTLD_DEFAULT, fn);
      }
    }
  }
  if (err > 2) {
    NOINTR(dup2(err, 2));
  }
  NOINTR(close(err));
  return rc;
}

static void loadSSL(void) {
  // SHELLINABOX_LIBSSL_SO can be used to select the specific
  // soname of libssl for systems where it is not libssl.so.
  // The feature is currently disabled.
  const char* path_libssl = NULL; // = getenv ("SHELLINABOX_LIBSSL_SO");
  if (path_libssl == NULL)
    path_libssl = "libssl.so";
  check(!SSL_library_init);
  struct {
    union {
      void *avoid_gcc_warning_about_type_punning;
      void **var;
    };
    const char *fn;
  } symbols[] = {
    { { &BIO_ctrl },                    "BIO_ctrl" },
    { { &BIO_f_buffer },                "BIO_f_buffer" },
    { { &BIO_free_all },                "BIO_free_all" },
    { { &BIO_new },                     "BIO_new" },
    { { &BIO_new_socket },              "BIO_new_socket" },
    { { &BIO_pop },                     "BIO_pop" },
    { { &BIO_push },                    "BIO_push" },
    { { &ERR_clear_error },             "ERR_clear_error" },
    { { &ERR_clear_error },             "ERR_clear_error" },
    { { &ERR_peek_error },              "ERR_peek_error" },
    { { &ERR_peek_error },              "ERR_peek_error" },
    { { &SSL_CTX_callback_ctrl },       "SSL_CTX_callback_ctrl" },
    { { &SSL_CTX_check_private_key },   "SSL_CTX_check_private_key" },
    { { &SSL_CTX_ctrl },                "SSL_CTX_ctrl" },
    { { &SSL_CTX_free },                "SSL_CTX_free" },
    { { &SSL_CTX_new },                 "SSL_CTX_new" },
    { { &SSL_CTX_use_PrivateKey_file }, "SSL_CTX_use_PrivateKey_file" },
    { { &SSL_CTX_use_PrivateKey_ASN1 }, "SSL_CTX_use_PrivateKey_ASN1" },
    { { &SSL_CTX_use_certificate_file },"SSL_CTX_use_certificate_file"},
    { { &SSL_CTX_use_certificate_ASN1 },"SSL_CTX_use_certificate_ASN1"},
    { { &SSL_ctrl },                    "SSL_ctrl" },
    { { &SSL_free },                    "SSL_free" },
    { { &SSL_get_error },               "SSL_get_error" },
    { { &SSL_get_ex_data },             "SSL_get_ex_data" },
    { { &SSL_get_rbio },                "SSL_get_rbio" },
#ifdef HAVE_TLSEXT
    { { &SSL_get_servername },          "SSL_get_servername" },
#endif
    { { &SSL_get_wbio },                "SSL_get_wbio" },
    { { &SSL_library_init },            "SSL_library_init" },
    { { &SSL_new },                     "SSL_new" },
    { { &SSL_read },                    "SSL_read" },
#ifdef HAVE_TLSEXT
    { { &SSL_set_SSL_CTX },             "SSL_set_SSL_CTX" },
#endif
    { { &SSL_set_accept_state },        "SSL_set_accept_state" },
    { { &SSL_set_bio },                 "SSL_set_bio" },
    { { &SSL_set_ex_data },             "SSL_set_ex_data" },
    { { &SSL_shutdown },                "SSL_shutdown" },
    { { &SSL_write },                   "SSL_write" },
    { { &SSLv23_server_method },        "SSLv23_server_method" },
    { { &d2i_X509 },                    "d2i_X509" },
    { { &X509_free },                   "X509_free" }
  };
  for (unsigned i = 0; i < sizeof(symbols)/sizeof(symbols[0]); i++) {
    if (!(*symbols[i].var = loadSymbol(path_libssl, symbols[i].fn))) {
      debug("Failed to load SSL support. Could not find \"%s\"",
            symbols[i].fn);
      for (unsigned j = 0; j < sizeof(symbols)/sizeof(symbols[0]); j++) {
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
#if defined(HAVE_OPENSSL) && !defined(HAVE_DLOPEN)
  return SSL_library_init();
#else
#if defined(HAVE_OPENSSL)
  // We want to call loadSSL() exactly once. For single-threaded applications,
  // this is straight-forward. For threaded applications, we need to call
  // pthread_once(), instead. We perform run-time checks for whether we are
  // single- or multi-threaded, so that the same code can be used.
  // This currently only works on Linux.
#if defined(HAVE_PTHREAD_H) && defined(__linux__) && defined(__i386__)
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
#endif
}

#if defined(HAVE_OPENSSL)
static void sslGenerateCertificate(const char *certificate,
                                   const char *serverName) {
 debug("Auto-generating missing certificate \"%s\" for \"%s\"",
       certificate, serverName);

  pid_t pid = fork();
  if (pid == -1) {
    warn("Failed to generate self-signed certificate \"%s\"", certificate);
  } else if (pid == 0) {
    int fd = NOINTR(open("/dev/null", O_RDONLY));
    check(fd != -1);
    check(NOINTR(dup2(fd, STDERR_FILENO)) == STDERR_FILENO);
    check(NOINTR(close(fd)) == 0);
    fd = NOINTR(open("/dev/null", O_WRONLY));
    check(fd != -1);
    check(NOINTR(dup2(fd, STDIN_FILENO)) == STDIN_FILENO);
    check(NOINTR(close(fd)) == 0);
    umask(077);
    check(setenv("PATH", "/usr/bin:/usr/sbin", 1) == 0);
    execlp("openssl", "openssl", "req", "-x509", "-nodes", "-days", "7300",
           "-newkey", "rsa:2048", "-keyout", certificate, "-out", certificate,
           "-subj", stringPrintf(NULL, "/CN=%s/", serverName),
           (char *)NULL);
    check(0);
  } else {
    int status;
    check(NOINTR(waitpid(pid, &status, 0)) == pid);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
      warn("Failed to generate self-signed certificate \"%s\"", certificate);
  }
}

static const unsigned char *sslSecureReadASCIIFileToMem(int fd) {
  size_t inc          = 16384;
  size_t bufSize      = inc;
  size_t len          = 0;
  unsigned char *buf;
  check((buf          = malloc(bufSize)) != NULL);
  for (;;) {
    check(len < bufSize - 1);
    ssize_t readLen   = bufSize - len - 1;
    ssize_t bytesRead = NOINTR(read(fd, buf + len, readLen));
    if (bytesRead > 0) {
      len            += bytesRead;
    }
    if (bytesRead != readLen) {
      break;
    }

    // Instead of calling realloc(), allocate a new buffer, copy the data,
    // and then clear the old buffer. This way, we are not accidentally
    // leaving key material in memory.
    unsigned char *newBuf;
    check((newBuf     = malloc(bufSize + inc)) != NULL);
    memcpy(newBuf, buf, len);
    memset(buf, 0, bufSize);
    free(buf);
    buf               = newBuf;
    bufSize          += inc;
  }
  check(len < bufSize);
  buf[len]            = '\000';
  return buf;
}

static const unsigned char *sslPEMtoASN1(const unsigned char *pem,
                                         const char *record,
                                         long *size,
                                         const unsigned char **eor) {
  if (eor) {
    *eor             = NULL;
  }
  *size              = 0;
  char *marker;
  check((marker      = stringPrintf(NULL, "-----BEGIN %s-----",record))!=NULL);
  unsigned char *ptr = (unsigned char *)strstr((char *)pem, marker);
  if (!ptr) {
    free(marker);
    return NULL;
  } else {
    ptr             += strlen(marker);
  }
  *marker            = '\000';
  check((marker      = stringPrintf(marker, "-----END %s-----",record))!=NULL);
  unsigned char *end = (unsigned char *)strstr((char *)ptr, marker);
  if (eor) {
    *eor             = end + strlen(marker);
  }
  free(marker);
  if (!end) {
    return NULL;
  }
  unsigned char *ret;
  ssize_t maxSize    = (((end - ptr)*6)+7)/8;
  check((ret         = malloc(maxSize)) != NULL);
  unsigned char *out = ret;
  unsigned bits      = 0;
  int count          = 0;
  while (ptr < end) {
    unsigned char ch = *ptr++;
    if (ch >= 'A' && ch <= 'Z') {
      ch            -= 'A';
    } else if (ch >= 'a' && ch <= 'z') {
      ch            -= 'a' - 26;
    } else if (ch >= '0' && ch <= '9') {
      ch            += 52 - '0';
    } else if (ch == '+') {
      ch            += 62 - '+';
    } else if (ch == '/') {
      ch            += 63 - '/';
    } else if (ch == '=') {
      while (ptr < end) {
        if ((ch      = *ptr++) != '=' && ch > ' ') {
          goto err;
        }
      }
      break;
    } else if (ch <= ' ') {
      continue;
    } else {
   err:
      free(ret);
      return NULL;
    }
    check(ch <= 63);
    check(count >= 0);
    check(count <= 6);
    bits             = (bits << 6) | ch;
    count           += 6;
    if (count >= 8) {
      *out++         = (bits >> (count -= 8)) & 0xFF;
    }
  }
  check(out - ret <= maxSize);
  *size              = out - ret;
  return ret;
}

static int sslSetCertificateFromFd(SSL_CTX *context, int fd) {
  int rc                       = 0;
  check(serverSupportsSSL());
  check(fd >= 0);
  const unsigned char *data    = sslSecureReadASCIIFileToMem(fd);
  check(!NOINTR(close(fd)));
  long dataSize                = (long)strlen((const char *)data);
  long certSize, rsaSize, dsaSize, ecSize, notypeSize;
  const unsigned char *record;
  const unsigned char *cert    = sslPEMtoASN1(data, "CERTIFICATE", &certSize,
                                              &record);
  const unsigned char *rsa     = sslPEMtoASN1(data, "RSA PRIVATE KEY",&rsaSize,
                                              NULL);
  const unsigned char *dsa     = sslPEMtoASN1(data, "DSA PRIVATE KEY",&dsaSize,
                                              NULL);
  const unsigned char *ec      = sslPEMtoASN1(data, "EC PRIVATE KEY",  &ecSize,
                                              NULL);
  const unsigned char *notype  = sslPEMtoASN1(data, "PRIVATE KEY", &notypeSize,
                                              NULL);
  if (certSize && (rsaSize || dsaSize
#ifdef EVP_PKEY_EC
                                      || ecSize
#endif
                                                || notypeSize) &&
      SSL_CTX_use_certificate_ASN1(context, certSize, cert) &&
      (!rsaSize ||
       SSL_CTX_use_PrivateKey_ASN1(EVP_PKEY_RSA, context, rsa, rsaSize)) &&
      (!dsaSize ||
       SSL_CTX_use_PrivateKey_ASN1(EVP_PKEY_DSA, context, dsa, dsaSize)) &&
#ifdef EVP_PKEY_EC
      (!ecSize ||
       SSL_CTX_use_PrivateKey_ASN1(EVP_PKEY_EC, context, ec, ecSize)) &&
#endif
      // Assume a private key is RSA if the header does not specify a type.
      // (e.g. BEGIN PRIVATE KEY)
      (!notypeSize ||
       SSL_CTX_use_PrivateKey_ASN1(EVP_PKEY_RSA, context, notype, notypeSize))
      ) {
    memset((char *)cert, 0, certSize);
    free((char *)cert);
    while (record) {
      cert                     = sslPEMtoASN1(record, "CERTIFICATE", &certSize,
                                              &record);
      if (cert) {
        X509 *x509;
        const unsigned char *c = cert;
        check(x509             = d2i_X509(NULL, &c, certSize));
        memset((char *)cert, 0, certSize);
        free((char *)cert);
        if (!SSL_CTX_add_extra_chain_cert(context, x509)) {
          X509_free(x509);
          break;
        }
      }
    }
    if (!record && SSL_CTX_check_private_key(context)) {
      rc                       = 1;
    }
    dcheck(!ERR_peek_error());
    ERR_clear_error();
  } else {
    memset((char *)cert, 0, certSize);
    free((char *)cert);
  }
  memset((char *)data, 0, dataSize);
  free((char *)data);
  memset((char *)rsa, 0, rsaSize);
  free((char *)rsa);
  memset((char *)dsa, 0, dsaSize);
  free((char *)dsa);
  memset((char *)ec, 0, ecSize);
  free((char *)ec);
  memset((char *)notype, 0, notypeSize);
  free((char *)notype);
  return rc;
}

static int sslSetCertificateFromFile(SSL_CTX *context,
                                     const char *filename) {
  int fd = open(filename, O_RDONLY);
  if (fd < 0) {
    return -1;
  }
  int rc = sslSetCertificateFromFd(context, fd);
  return rc;
}
#endif

#ifdef HAVE_TLSEXT
static int sslSNICallback(SSL *sslHndl, int *al ATTR_UNUSED,
                          struct SSLSupport *ssl) {
  UNUSED(al);
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
      free(serverName);
      return SSL_TLSEXT_ERR_OK;
    }
    serverName[++i]       = ch;
    if (!ch) {
      break;
    }
  }
  SSL_CTX *context        = (SSL_CTX *)getFromTrie(&ssl->sniContexts,
                                                   serverName+1,
                                                   NULL);
  if (context == NULL) {
    check(context         = SSL_CTX_new(SSLv23_server_method()));
    check(ssl->sniCertificatePattern);
    char *certificate     = stringPrintfUnchecked(NULL,
                                                  ssl->sniCertificatePattern,
                                                  serverName);
    if (sslSetCertificateFromFile(context, certificate) < 0) {
      if (ssl->generateMissing) {
        sslGenerateCertificate(certificate, serverName + 1);

        // No need to check the certificate. If we fail to set it, we will use
        // the default certificate, instead.
        sslSetCertificateFromFile(context, certificate);
      } else {
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
    check(SSL_set_SSL_CTX(sslHndl, context));
  }
  check(!ERR_peek_error());
  return SSL_TLSEXT_ERR_OK;
}
#endif

#if defined(HAVE_OPENSSL) && !defined(HAVE_GETHOSTBYNAME_R)
// This is a not-thread-safe replacement for gethostbyname_r()
#define gethostbyname_r x_gethostbyname_r
static int gethostbyname_r(const char *name, struct hostent *ret,
                           char *buf ATTR_UNUSED, size_t buflen ATTR_UNUSED,
                           struct hostent **result, int *h_errnop) {
  UNUSED(buf);
  UNUSED(buflen);
  if (result) {
    *result          = NULL;
  }
  if (h_errnop) {
    *h_errnop        = ERANGE;
  }
  if (!ret) {
    return -1;
  }
  struct hostent *he = gethostbyname(name);
  if (he) {
    *ret             = *he;
    if (result) {
      *result        = ret;
    }
  }
  if (h_errnop) {
    *h_errnop        = h_errno;
  }
  return he ? 0 : -1;
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

  // Try to set the default certificate. If necessary, (re-)generate it.
  check(ssl->sslContext              = SSL_CTX_new(SSLv23_server_method()));
  if (autoGenerateMissing) {
    if (sslSetCertificateFromFile(ssl->sslContext, defaultCertificate) < 0) {
      char hostname[256], buf[4096];
      check(!gethostname(hostname, sizeof(hostname)));
      struct hostent he_buf, *he;
      int h_err = 0;
      int ret = gethostbyname_r(hostname, &he_buf, buf, sizeof(buf), &he, &h_err);
      if (!ret && he && he->h_name) {
        sslGenerateCertificate(defaultCertificate, he->h_name);
      } else {
        if (h_err) {
          warn("Error getting host information: \"%s\".", hstrerror(h_err));
        }
        sslGenerateCertificate(defaultCertificate, hostname);
      }
    } else {
      goto valid_certificate;
    }
  }
  if (sslSetCertificateFromFile(ssl->sslContext, defaultCertificate) < 0) {
    fatal("Cannot read valid certificate from \"%s\". "
          "Check file permissions and file format.", defaultCertificate);
  }
 valid_certificate:
  free(defaultCertificate);

  // Enable SNI support so that we can set a different certificate, if the
  // client asked for it.
#ifdef HAVE_TLSEXT
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

// Convert the file descriptor to a human-readable format. Attempts to
// retrieve the original file name where possible.
#ifdef HAVE_OPENSSL
static char *sslFdToFilename(int fd) {
  char *proc, *buf;
  int  len         = 128;
  check(proc       = stringPrintf(NULL, "/proc/self/fd/%d", fd));
  check(buf        = malloc(len));
  for (;;) {
    ssize_t i;
    if ((i = readlink(proc, buf + 1, len-3)) < 0) {
      free(proc);
      free(buf);
      check(buf    = stringPrintf(NULL, "fd %d", fd));
      return buf;
    } else if (i >= len-3) {
      len         += 512;
      check(buf    = realloc(buf, len));
    } else {
      free(proc);
      check(i >= 0 && i < len);
      buf[i+1]     = '\000';
      struct stat sb;
      if (!stat(buf + 1, &sb) && S_ISREG(sb.st_mode)) {
        *buf       = '"';
        buf[i + 1] = '"';
        buf[i + 2] = '\000';
        return buf;
      } else {
        free(buf);
        check(buf  = stringPrintf(NULL, "fd %d", fd));
        return buf;
      }
    }
  }
}
#endif

void sslSetCertificateFd(struct SSLSupport *ssl, int fd) {
#ifdef HAVE_OPENSSL
  check(ssl->sslContext = SSL_CTX_new(SSLv23_server_method()));
  char *filename = sslFdToFilename(fd);
  if (!sslSetCertificateFromFd(ssl->sslContext, fd)) {
    fatal("Cannot read valid certificate from %s. Check file format.",
          filename);
  }
  free(filename);
  ssl->generateMissing  = 0;
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
#if defined(HAVE_PTHREAD_H) && defined(__linux__) && defined(__i386__)
  if (&pthread_sigmask) {
    dcheck(!pthread_sigmask(SIG_BLOCK, &set, NULL));
  } else
#endif
  {
    dcheck(!sigprocmask(SIG_BLOCK, &set, NULL));
  }
}

#ifndef HAVE_SIGWAIT
// This is a non-thread-safe replacement for sigwait()
static int dummysignalno;
static void dummysignal(int signo) {
  dummysignalno = signo;
}

#define sigwait x_sigwait
static int sigwait(const sigset_t *set, int *sig) {
  sigset_t mask, old_mask;
  sigfillset(&mask);
#if defined(HAVE_PTHREAD_H) && defined(__linux__) && defined(__i386__)
  if (&pthread_sigmask) {
    dcheck(!pthread_sigmask(SIG_BLOCK, &mask, &old_mask));
  } else
#endif
  {
    dcheck(!sigprocmask(SIG_BLOCK, &mask, &old_mask));
  }
  #ifndef NSIG
  #define NSIG 32
  #endif
  struct sigaction sa[NSIG];
  memset(sa, 0, sizeof(sa));
  sa->sa_handler = dummysignal;
  for (int i = 1; i <= NSIG; i++) {
    if (sigismember(set, i)) {
      sigdelset(&mask, i);
      sigaction(i, sa, sa + i);
    }
  }
  dummysignalno = -1;
  sigsuspend(&mask);
#if defined(HAVE_PTHREAD_H) && defined(__linux__) && defined(__i386__)
  if (&pthread_sigmask) {
    dcheck(!pthread_sigmask(SIG_SETMASK, &old_mask, NULL));
  } else
#endif
  {
    dcheck(!sigprocmask(SIG_BLOCK, &old_mask, NULL));
  }
  return dummysignalno;
}
#endif

int sslUnblockSigPipe(void) {
  int signum = 0;
  sigset_t set;
  check(!sigpending(&set));
  if (sigismember(&set, SIGPIPE)) {
    sigwait(&set, &signum);
  }
  sigemptyset(&set);
  sigaddset(&set, SIGPIPE);
#if defined(HAVE_PTHREAD_H) && defined(__linux__) && defined(__i386__)
  if (&pthread_sigmask) {
    dcheck(!pthread_sigmask(SIG_UNBLOCK, &set, NULL));
  } else
#endif
  {
    dcheck(!sigprocmask(SIG_UNBLOCK, &set, NULL));
  }
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
