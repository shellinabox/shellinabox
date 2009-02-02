AC_PREREQ(2.57)
AC_INIT(shellinabox, 2.3, markus@shellinabox.com)
AM_INIT_AUTOMAKE
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AC_C_CONST
AC_PROG_GCC_TRADITIONAL
AC_CHECK_HEADERS([openssl/bio.h openssl/err.h openssl/ssl.h pthread.h         \
                  security/pam_appl.h security/pam_misc.h sys/prctrl.h        \
                  utmpx.h])
AC_CHECK_FUNCS(dlopen, [],
               [AC_CHECK_LIB(dl, dlopen, LIBS="-ldl $LIBS")])
AC_TRY_LINK([#ifndef _XOPEN_SOURCE
             #define _XOPEN_SOURCE
             #endif
             #ifndef _GNU_SOURCE
             #define _GNU_SOURCE
             #endif
             #include <stdlib.h>],
            [ptsname_r(0, 0, 0);],
            [AC_DEFINE(HAVE_PTSNAME_R, 1,
                     Define to 1 if you have a re-entrant version of ptsname)])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
