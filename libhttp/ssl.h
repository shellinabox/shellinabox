// ssl.h -- Support functions that find and load SSL support, if available
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

#ifndef SSL_H__
#define SSL_H__

#include "config.h"

#include "libhttp/trie.h"

#if defined(HAVE_OPENSSL_BIO_H) && \
    defined(HAVE_OPENSSL_ERR_H) && \
    defined(HAVE_OPENSSL_SSL_H)
#define HAVE_OPENSSL 1
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#else
#undef HAVE_OPENSSL
typedef struct BIO        BIO;
typedef struct BIO_METHOD BIO_METHOD;
typedef struct SSL        SSL;
typedef struct SSL_CTX    SSL_CTX;
typedef struct SSL_METHOD SSL_METHOD;
typedef struct X509       X509;
#define SSL_ERROR_WANT_READ  2
#define SSL_ERROR_WANT_WRITE 3
#endif

#if defined(HAVE_DLOPEN)
extern long    (*x_BIO_ctrl)(BIO *, int, long, void *);
extern BIO_METHOD *(*x_BIO_f_buffer)(void);
extern void    (*x_BIO_free_all)(BIO *);
extern BIO    *(*x_BIO_new)(BIO_METHOD *);
extern BIO    *(*x_BIO_new_socket)(int, int);
extern BIO    *(*x_BIO_pop)(BIO *);
extern BIO    *(*x_BIO_push)(BIO *, BIO *);
extern void    (*x_ERR_clear_error)(void);
extern void    (*x_ERR_clear_error)(void);
extern unsigned long (*x_ERR_peek_error)(void);
extern unsigned long (*x_ERR_peek_error)(void);
extern long    (*x_SSL_CTX_callback_ctrl)(SSL_CTX *, int, void (*)(void));
extern int     (*x_SSL_CTX_check_private_key)(const SSL_CTX *);
extern long    (*x_SSL_CTX_ctrl)(SSL_CTX *, int, long, void *);
extern void    (*x_SSL_CTX_free)(SSL_CTX *);
extern SSL_CTX*(*x_SSL_CTX_new)(SSL_METHOD *);
extern int     (*x_SSL_CTX_use_PrivateKey_file)(SSL_CTX *, const char *, int);
extern int     (*x_SSL_CTX_use_PrivateKey_ASN1)(int, SSL_CTX *,
                                                const unsigned char *, long);
extern int     (*x_SSL_CTX_use_certificate_file)(SSL_CTX *, const char *, int);
extern int     (*x_SSL_CTX_use_certificate_ASN1)(SSL_CTX *, long,
                                                 const unsigned char *);
extern long    (*x_SSL_ctrl)(SSL *, int, long, void *);
extern void    (*x_SSL_free)(SSL *);
extern int     (*x_SSL_get_error)(const SSL *, int);
extern void   *(*x_SSL_get_ex_data)(const SSL *, int);
extern BIO    *(*x_SSL_get_rbio)(const SSL *);
extern const char *(*x_SSL_get_servername)(const SSL *, int);
extern BIO    *(*x_SSL_get_wbio)(const SSL *);
extern int     (*x_SSL_library_init)(void);
extern SSL    *(*x_SSL_new)(SSL_CTX *);
extern int     (*x_SSL_read)(SSL *, void *, int);
extern SSL_CTX*(*x_SSL_set_SSL_CTX)(SSL *, SSL_CTX *);
extern void    (*x_SSL_set_accept_state)(SSL *);
extern void    (*x_SSL_set_bio)(SSL *, BIO *, BIO *);
extern int     (*x_SSL_set_ex_data)(SSL *, int, void *);
extern int     (*x_SSL_shutdown)(SSL *);
extern int     (*x_SSL_write)(SSL *, const void *, int);
extern SSL_METHOD *(*x_SSLv23_server_method)(void);
extern X509 *  (*x_d2i_X509)(X509 **px, const unsigned char **in, int len);
extern void    (*x_X509_free)(X509 *a);

#define BIO_ctrl                     x_BIO_ctrl
#define BIO_f_buffer                 x_BIO_f_buffer
#define BIO_free_all                 x_BIO_free_all
#define BIO_new                      x_BIO_new
#define BIO_new_socket               x_BIO_new_socket
#define BIO_pop                      x_BIO_pop
#define BIO_push                     x_BIO_push
#define ERR_clear_error              x_ERR_clear_error
#define ERR_clear_error              x_ERR_clear_error
#define ERR_peek_error               x_ERR_peek_error
#define ERR_peek_error               x_ERR_peek_error
#define SSL_CTX_callback_ctrl        x_SSL_CTX_callback_ctrl
#define SSL_CTX_check_private_key    x_SSL_CTX_check_private_key
#define SSL_CTX_ctrl                 x_SSL_CTX_ctrl
#define SSL_CTX_free                 x_SSL_CTX_free
#define SSL_CTX_new                  x_SSL_CTX_new
#define SSL_CTX_use_PrivateKey_file  x_SSL_CTX_use_PrivateKey_file
#define SSL_CTX_use_PrivateKey_ASN1  x_SSL_CTX_use_PrivateKey_ASN1
#define SSL_CTX_use_certificate_file x_SSL_CTX_use_certificate_file
#define SSL_CTX_use_certificate_ASN1 x_SSL_CTX_use_certificate_ASN1
#define SSL_ctrl                     x_SSL_ctrl
#define SSL_free                     x_SSL_free
#define SSL_get_error                x_SSL_get_error
#define SSL_get_ex_data              x_SSL_get_ex_data
#define SSL_get_rbio                 x_SSL_get_rbio
#define SSL_get_servername           x_SSL_get_servername
#define SSL_get_wbio                 x_SSL_get_wbio
#define SSL_library_init             x_SSL_library_init
#define SSL_new                      x_SSL_new
#define SSL_read                     x_SSL_read
#define SSL_set_SSL_CTX              x_SSL_set_SSL_CTX
#define SSL_set_accept_state         x_SSL_set_accept_state
#define SSL_set_bio                  x_SSL_set_bio
#define SSL_set_ex_data              x_SSL_set_ex_data
#define SSL_shutdown                 x_SSL_shutdown
#define SSL_write                    x_SSL_write
#define SSLv23_server_method         x_SSLv23_server_method
#define d2i_X509                     x_d2i_X509
#define X509_free                    x_X509_free

#undef  BIO_set_buffer_read_data
#undef  SSL_CTX_set_tlsext_servername_arg
#undef  SSL_CTX_set_tlsext_servername_callback
#undef  SSL_get_app_data
#undef  SSL_set_app_data
#undef  SSL_set_mode
#define BIO_set_buffer_read_data(b, buf, num)                                 \
                                 (x_BIO_ctrl(b, BIO_C_SET_BUFF_READ_DATA,     \
                                             num, buf))
#define SSL_CTX_set_tlsext_servername_arg(ctx, arg)                           \
                                 (x_SSL_CTX_ctrl(ctx,                         \
                                          SSL_CTRL_SET_TLSEXT_SERVERNAME_ARG, \
                                          0, (void *)arg))
#define SSL_CTX_set_tlsext_servername_callback(ctx, cb)                       \
                                 (x_SSL_CTX_callback_ctrl(ctx,                \
                                           SSL_CTRL_SET_TLSEXT_SERVERNAME_CB, \
                                           (void (*)(void))cb))
#define SSL_get_app_data(s)      (x_SSL_get_ex_data(s, 0))
#define SSL_set_app_data(s, arg) (x_SSL_set_ex_data(s, 0, (char *)arg))
#define SSL_set_mode(ssl, op)    (x_SSL_ctrl((ssl), SSL_CTRL_MODE, (op), NULL))
#endif

struct SSLSupport {
  int         enabled;
  SSL_CTX     *sslContext;
  char        *sniCertificatePattern;
  int         generateMissing;
  struct Trie sniContexts;
};

int  serverSupportsSSL(void);
struct SSLSupport *newSSL();
void initSSL(struct SSLSupport *ssl);
void destroySSL(struct SSLSupport *ssl);
void deleteSSL(struct SSLSupport *ssl);
void sslSetCertificate(struct SSLSupport *ssl, const char *filename,
                       int autoGenerateMissing);
void sslSetCertificateFd(struct SSLSupport *ssl, int fd);
int  sslEnable(struct SSLSupport *ssl, int enabled);
void sslBlockSigPipe();
int  sslUnblockSigPipe();
int  sslPromoteToSSL(struct SSLSupport *ssl, SSL **sslHndl, int fd,
                     const char *buf, int len);
void sslFreeHndl(SSL **sslHndl);

#endif
