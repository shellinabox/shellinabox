AC_PREREQ(2.57)

dnl This is the one location where the authoritative version number is stored
AC_INIT(shellinabox, 2.10, markus@shellinabox.com)
VCS_REVISION=239
AC_SUBST(VCS_REVISION)
AC_DEFINE_UNQUOTED(VCS_REVISION, "${VCS_REVISION}",
                   [Most recent revision number in the version control system])

dnl Set up autoconf/automake for building C libraries and binaries with GCC
CFLAGS="${CFLAGS:--Os}"
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AC_PROG_CC
AC_LANG_WERROR
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_SUBST(LIBTOOL_DEPS)
AC_C_CONST
AC_PROG_GCC_TRADITIONAL

dnl Check for header files that do not exist on all platforms
AC_CHECK_HEADERS([libutil.h pthread.h pty.h strings.h sys/prctl.h sys/uio.h  \
                  util.h utmp.h utmpx.h])

dnl Most systems require linking against libutil.so in order to get login_tty()
AC_CHECK_FUNCS(login_tty, [],
               [AC_CHECK_LIB(util, login_tty,
                [LIBS="-lutil $LIBS"
                 AC_DEFINE(HAVE_LOGIN_TTY)])])

dnl Use strlcat() instead of strncat() to avoid spurious warnings
AC_CHECK_FUNCS([strlcat])

dnl Prefer thread-safe functions, if available
AC_CHECK_FUNCS([getgrgid_r getgrnam_r gethostbyname_r getpwnam_r getpwuid_r  \
                openpty strcasestr getresuid getresgid setresuid setresgid ])

dnl We prefer ptsname_r(), but will settle for ptsname() if necessary
AC_TRY_LINK([#ifndef _XOPEN_SOURCE
             #define _XOPEN_SOURCE
             #endif
             #ifndef _GNU_SOURCE
             #define _GNU_SOURCE
             #endif
             #include <stdlib.h>],
            [char buf[10]; ptsname_r(0, buf, sizeof(buf));],
            [AC_DEFINE(HAVE_PTSNAME_R, 1,
                     Define to 1 if you have a re-entrant version of ptsname)])

dnl Apparently, some systems define sigwait() but fail to implement it
AC_TRY_LINK([#include <pthread.h>
             #include <signal.h>],
            [sigset_t s; int n; sigwait(&s, &n);],
            [AC_DEFINE(HAVE_SIGWAIT, 1,
                       Define to 1 if you have a working sigwait)])

dnl Not every system has support for isnan()
AC_TRY_LINK([#include <math.h>],
            [if (isnan(0.0)) return 1;],
            [AC_DEFINE(HAVE_ISNAN, 1,
                       Define to 1 if you have support for isnan)])

dnl Even if utmpx.h exists, not all systems have support for updwtmpx()
AC_TRY_LINK([#include <utmp.h>],
            [updwtmp(0, 0);],
            [AC_DEFINE(HAVE_UPDWTMP, 1,
                       Define to 1 if you have support for updwtmp)])
AC_TRY_LINK([#include <utmpx.h>],
            [updwtmpx(0, 0);],
            [AC_DEFINE(HAVE_UPDWTMPX, 1,
                       Define to 1 if you have support for updwtmpx)])

dnl Check if the compiler supports aliasing of symbols
AC_TRY_LINK([void x(void) { };
             void y(void) __attribute__((alias("x")));],
            [y();],
            [AC_DEFINE(HAVE_ATTRIBUTE_ALIAS, 1,
                       Define to 1 if you have support for symbol aliasing)])

dnl Check if the compiler has support to mark parameters as unused
AC_TRY_LINK([void x(int i __attribute__((unused))) __attribute__((unused));],
            [],
            [AC_DEFINE(HAVE_ATTRIBUTE_UNUSED, 1,
                  Define to 1 if you have support for the "unused" attribute)])

dnl Check the function signature of getgrouplist()
AC_TRY_LINK([#define _BSD_SOURCE
             #include <grp.h>
             #include <unistd.h>],
            [int (*f)(const char *, int, int *, int *) = getgrouplist;],
            [AC_DEFINE(HAVE_GETGROUPLIST_TAKES_INTS, 1,
                       Define to 1 if getgrouplist() takes ints as arguments)])

dnl On some systems, calling /bin/login does not work. Disable the LOGIN
dnl feature, if the user tells us that it does not do the right thing.
AC_ARG_ENABLE(login,
              [  --disable-login           on some systems (e.g. Fedora), calling /bin/login
                            does not work well. If you know that your system
                            suffers from this problem, set this option to
                            remove support for the LOGIN keyword in the
                            service description.])
if test "x$enable_login" != xno; then
  AC_DEFINE(HAVE_BIN_LOGIN, 1,
                                Set if you want support for calling /bin/login)
fi

dnl We automatically detect SSL support, but allow users to disable it
AC_ARG_ENABLE(ssl,
              [  --disable-ssl             if available at built-time, support for SSL
                            connections will be enabled. It can still be
                            disabled at run-time, either on the daemon's
                            command line or if the operating system does not
                            have the OpenSSL libraries available.])

dnl We automatically detect PAM support, but allow users to disable it
AC_ARG_ENABLE(pam,
              [  --disable-pam             PAM support is necessary in order to authenticate
                            users for running programs other than their default
                            login shell.])

dnl We try to always use dlopen() instead of linking libraries dynamically, as
dnl this reduces the hard run-time dependencies that our binary has. But we
dnl allow users to disable this feature.
AC_ARG_ENABLE(runtime-loading,
              [  --disable-runtime-loading ShellInABox will try to load the OpenSSL, and PAM
                            libraries at run-time, if it has been compiled with
                            support for these libraries, and if the operating
                            system supports dynamic loading of libraries. This
                            allows you to install the same binary on different
                            systems independent of whether they have OpenSSL
                            and PAM enabled.  If you would rather directly link
                            these libraries into the binary, thus making them a
                            hard dependency, then disable runtime-loading.])

dnl Only test for OpenSSL headers, if not explicitly disabled
if test "x$enable_ssl" != xno; then
  AC_CHECK_HEADERS([openssl/bio.h openssl/err.h openssl/ssl.h])
fi

dnl Only test for PAM headers, if not explicitly disabled
if test "x$enable_pam" != xno; then
  AC_CHECK_HEADERS([security/pam_appl.h security/pam_client.h                 \
                    security/pam_misc.h ])
fi

dnl Only test for dlopen(), if not explicitly disabled. Add required libdl.so
dnl library if necessary. Also, if dlopen() is not available on this system,
dnl explicitly disable runtime loading.
if test "x$enable_runtime_loading" != xno; then
  AC_CHECK_FUNCS(dlopen, [],
                 [AC_CHECK_LIB(dl, dlopen,
                               [LIBS="-ldl $LIBS"
                                AC_DEFINE(HAVE_DLOPEN)],
                               [enable_runtime_loading=no])])
fi

dnl If runtime loading has been disabled, add OpenSSL and PAM as hard
dnl dependencies.
if test "x$enable_runtime_loading" == xno; then
  dnl Link against OpenSSL libraries, unless SSL support has been disabled
  if test "x$enable_ssl" != xno; then
    AC_CHECK_HEADER(openssl/bio.h,
      [AC_CHECK_HEADER(openssl/err.h,
        [AC_CHECK_HEADER(openssl/ssl.h, [LIBS="-lssl -lcrypto $LIBS"])])])
  fi

  dnl Link against PAM libraries, unless PAM support has been disabled
  if test "x$enable_pam" != xno; then
    AC_CHECK_HEADER(security/pam_appl.h, [LIBS="-lpam $LIBS"])
    AC_CHECK_HEADER(security/pam_misc.h, [LIBS="-lpam_misc $LIBS"])
  fi
fi

AC_CHECK_LIB(z, deflate, [
  AC_CHECK_HEADER(zlib.h, [LIBS="-lz $LIBS"
                           AC_DEFINE(HAVE_ZLIB, 1,
                           Define to 1 if zlib development files are installed)
])])

dnl Generate output files
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
