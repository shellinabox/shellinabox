AM_CPPFLAGS          =
AM_CFLAGS            = -g -std=gnu99 -Wall
AM_LDFLAGS           = -g -lm

noinst_LTLIBRARIES   = libhttp.la                                             \
                       liblogging.la
noinst_DATA          = $(top_srcdir)/demo/demo.js
bin_PROGRAMS         = shellinaboxd
man_MANS             = shellinaboxd.1
noinst_HEADERS       = libhttp/http.h
dist_doc_DATA        = AUTHORS                                                \
                       COPYING                                                \
                       GPL-2                                                  \
                       ChangeLog                                              \
                       INSTALL                                                \
                       NEWS                                                   \
                       README                                                 \
                       TODO                                                   \
                       shellinabox/white-on-black.css                         \
                       shellinabox/black-on-white.css                         \
                       shellinabox/monochrome.css                             \
                       shellinabox/color.css
EXTRA_DIST           = INSTALL.Debian                                         \
                       README.Fedora                                          \
                       demo/beep.wav                                          \
                       demo/favicon.ico                                       \
                       demo/demo.html                                         \
                       demo/demo.js                                           \
                       demo/demo.jspp                                         \
                       demo/demo.xml                                          \
                       demo/enabled.gif                                       \
                       demo/keyboard.html                                     \
                       demo/keyboard.png                                      \
                       demo/styles.css                                        \
                       demo/print-styles.css                                  \
                       demo/vt100.js                                          \
                       demo/usercss-0.css                                     \
                       demo/usercss-1.css                                     \
                       demo/usercss-2.css                                     \
                       demo/usercss-3.css                                     \
                       etc-pam.d-shellinabox-example                          \
                       shellinabox/shellinaboxd.man.in                        \
                       shellinabox/shell_in_a_box.js                          \
                       shellinabox/vt100.js                                   \
                       shellinabox/cgi-mode-example.sh                        \
                       debian/README                                          \
                       debian/README.available                                \
                       debian/README.enabled                                  \
                       debian/changelog                                       \
                       debian/compat                                          \
                       debian/control                                         \
                       debian/copyright                                       \
                       debian/docs                                            \
                       debian/rules                                           \
                       debian/shellinabox.default                             \
                       debian/shellinabox.dirs                                \
                       debian/shellinabox.init                                \
                       debian/shellinabox.install                             \
                       debian/shellinabox.postinst                            \
                       debian/shellinabox.postrm                              \
                       debian/source/format                                   \
                       debian/watch
LIBLOGGING_INCLUDES  = logging/logging.h                                      \
                       config.h
liblogging_la_SOURCES= logging/logging.c                                      \
                       $(LIBLOGGING_INCLUDES)
liblogging_la_LDFLAGS= -version 1:0:0

LIBHTTP_INCLUDES     = libhttp/hashmap.h                                      \
                       libhttp/trie.h                                         \
                       libhttp/httpconnection.h                               \
                       libhttp/server.h                                       \
                       libhttp/ssl.h                                          \
                       libhttp/url.h                                          \
                       config.h
libhttp_la_SOURCES   = libhttp/hashmap.c                                      \
                       libhttp/trie.c                                         \
                       libhttp/httpconnection.c                               \
                       libhttp/server.c                                       \
                       libhttp/ssl.c                                          \
                       libhttp/url.c                                          \
                       $(LIBHTTP_INCLUDES)                                    \
                       libhttp/libhttp.sym
libhttp_la_LDFLAGS   = -export-symbols  $(top_srcdir)/libhttp/libhttp.sym     \
                       -version 1:0:0

shellinaboxd_SOURCES = shellinabox/shellinaboxd.c                             \
                       shellinabox/externalfile.c                             \
                       shellinabox/externalfile.h                             \
                       shellinabox/launcher.c                                 \
                       shellinabox/launcher.h                                 \
                       shellinabox/privileges.c                               \
                       shellinabox/privileges.h                               \
                       shellinabox/service.c                                  \
                       shellinabox/service.h                                  \
                       shellinabox/session.c                                  \
                       shellinabox/session.h                                  \
                       shellinabox/usercss.c                                  \
                       shellinabox/usercss.h                                  \
                       shellinabox/cgi_root.html                              \
                       shellinabox/root_page.html                             \
                       shellinabox/vt100.jspp                                 \
                       shellinabox/shell_in_a_box.jspp                        \
                       shellinabox/styles.css                                 \
                       shellinabox/print-styles.css                           \
                       shellinabox/enabled.gif                                \
                       shellinabox/favicon.ico                                \
                       shellinabox/keyboard.png                               \
                       shellinabox/keyboard-layout.html                       \
                       shellinabox/beep.wav                                   \
                       config.h
BUILT_SOURCES        = shellinabox/beep.h                                     \
                       shellinabox/cgi_root.h                                 \
                       shellinabox/enabled.h                                  \
                       shellinabox/favicon.h                                  \
                       shellinabox/keyboard.h                                 \
                       shellinabox/keyboard-layout.h                          \
                       shellinabox/print-styles.h                             \
                       shellinabox/root_page.h                                \
                       shellinabox/shell_in_a_box.h                           \
                       shellinabox/shell_in_a_box.js                          \
                       shellinabox/styles.h                                   \
                       shellinabox/vt100.h                                    \
                       shellinabox/vt100.js


shellinaboxd_LDADD   = liblogging.la                                          \
                       libhttp.la
shellinaboxd_LDFLAGS = -static

symbolname           =                                                        \
  sed -e 's/.*\/\([^.]*\)[.].*/\1/'                                           \
      -e 't0'                                                                 \
      -e 's/\([^.]*\)[.].*/\1/'                                               \
      -e ':0'                                                                 \
      -e 's/$$/ aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ/'        \
      -e ':1'                                                                 \
      -e 's/\([^-_]*\)[-_]\([a-z]\)\([^ ]* .*\2\)\(.\)/\1\4\3\4/'             \
      -e 't1'                                                                 \
      -e 's/.\{53\}$$//'                                                      \
      -e 's/[-/.]/_/g'

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck


${top_srcdir}/demo/demo.js: ${top_srcdir}/demo/beep.wav                       \
                            ${top_srcdir}/demo/demo.jspp                      \
                            ${top_srcdir}/demo/enabled.gif                    \
                            ${top_srcdir}/demo/favicon.ico                    \
                            ${top_srcdir}/demo/keyboard.html                  \
                            ${top_srcdir}/demo/keyboard.png                   \
                            ${top_srcdir}/demo/styles.css                     \
                            ${top_srcdir}/demo/print-styles.css               \
                            ${top_srcdir}/demo/vt100.js                       \
                            ${top_srcdir}/demo/usercss-0.css                  \
                            ${top_srcdir}/demo/usercss-1.css                  \
                            ${top_srcdir}/demo/usercss-2.css                  \
                            ${top_srcdir}/demo/usercss-3.css

${top_srcdir}/demo/beep.wav: ${top_srcdir}/shellinabox/beep.wav
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/enabled.gif: ${top_srcdir}/shellinabox/enabled.gif
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/favicon.ico: ${top_srcdir}/shellinabox/favicon.ico
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/keyboard.html: ${top_srcdir}/shellinabox/keyboard-layout.html
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/keyboard.png: ${top_srcdir}/shellinabox/keyboard.png
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/styles.css: ${top_srcdir}/shellinabox/styles.css
	@rm -f "$@"
	sed -e '/\[if DEFINES_COLORS\]/,/\[endif DEFINES_COLORS\]/d' "$?" >"$@"

${top_srcdir}/demo/print-styles.css: ${top_srcdir}/shellinabox/print-styles.css
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/usercss-0.css: ${top_srcdir}/shellinabox/white-on-black.css
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/usercss-1.css: ${top_srcdir}/shellinabox/black-on-white.css
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/usercss-2.css: ${top_srcdir}/shellinabox/monochrome.css
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/usercss-3.css: ${top_srcdir}/shellinabox/color.css
	@rm -f "$@"
	ln "$?" "$@"

${top_srcdir}/demo/vt100.js: ${top_srcdir}/shellinabox/vt100.js
	@rm -f "$@"
	ln "$?" "$@"

shellinaboxd.1: ${top_srcdir}/shellinabox/shellinaboxd.man.in                 \
		${top_srcdir}/config.h
	@src="${top_srcdir}/shellinabox/shellinaboxd.man.in";                 \
	echo preprocess  "$$src" '>'"$@";                                     \
	if sed -e 's/^#define \([^ ]*\).*/\1/' -e t -e d config.h |           \
             grep 'HAVE_BIN_LOGIN' >/dev/null 2>&1; then                      \
	  sed -e '/^#ifndef  *HAVE_BIN_LOGIN$$/,/^#endif$$/d' "$$src";        \
	else                                                                  \
	  sed -e '/^#ifdef  *HAVE_BIN_LOGIN$$/,/^#endif$$/d' "$$src";         \
	fi |                                                                  \
	if [ `sed -e 's/^#define \([^ ]*\).*/\1/' -e t -e d config.h |        \
             egrep 'HAVE_OPENSSL_BIO_H|HAVE_OPENSSL_ERR_H|HAVE_OPENSSL_SSL_H'|\
             wc -l` -eq 3 ]; then                                             \
	  sed -e '/^#ifndef  *HAVE_OPENSSL$$/,/^#endif$$/d';                  \
	else                                                                  \
	  sed -e '/^#ifdef  *HAVE_OPENSSL$$/,/^#endif$$/d';                   \
	fi |                                                                  \
	if sed -e 's/^#define \([^ ]*\).*/\1/' -e t -e d config.h |           \
             grep 'HAVE_SECURITY_PAM_APPL_H' >/dev/null 2>&1; then            \
	  sed -e '/^#ifndef  *HAVE_PAM$$/,/^#endif$$/d';                      \
	else                                                                  \
	  sed -e '/^#ifdef  *HAVE_PAM$$/,/^#endif$$/d';                       \
	fi |                                                                  \
	if [ -n "${DPKGBUILD}" ]; then                                        \
	  sed -e '/^#ifndef  *DPKGBUILD$$/,/^#endif$$/d';                     \
	else                                                                  \
	  sed -e '/^#ifdef  *DPKGBUILD$$/,/^#endif$$/d';                      \
	fi |                                                                  \
	sed -e '/^#/d' >"$@"
	@out=`echo "$@" 2>/dev/null|sed -e 's/\.[^.]*$$/.ps/'`;               \
	man -Tps "./$@" >"$${out}" 2>/dev/null || rm -f "$${out}"

clean-local:
	-rm -rf shellinaboxd.1                                                \
	        shellinaboxd.ps
	-rm -rf debian/shellinabox                                            \
	        debian/shellinabox*.debhelper*                                \
	        debian/shellinabox.substvars                                  \
	        debian/tmp
	-rm -rf shellinabox/beep.h                                            \
	        shellinabox/cgi_root.h      				      \
	        shellinabox/enabled.h       				      \
	        shellinabox/favicon.h       				      \
	        shellinabox/keyboard.h      				      \
	        shellinabox/keyboard-layout.h				      \
	        shellinabox/print-styles.h  				      \
	        shellinabox/root_page.h     				      \
	        shellinabox/shell_in_a_box.h				      \
	        shellinabox/styles.h                                          \
	        shellinabox/vt100.h

.css.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"

.gif.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"

.png.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"

.html.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"


.ico.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"

.jspp.js:
	@echo preprocess "$<" "$@"
	@mkdir -p "`dirname "$@"`"
	sed -e "`sed -e 's/^#define *\([^ ]*\) *\(.*\)/\/^[^#]\/s\/\1\/\2 \\\\\/* \1 *\\\\\/\/g/' \
	             -e t                                                     \
	             -e d "$<"`"                                              \
	     -e "s/^#/\/\/ #/"                                                \
	     -e "s/VERSION/\"@VERSION@ (revision @VCS_REVISION@)\"/g"         \
	     "$<" >"$@"

.js.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"


.wav.h:
	@echo od "$<" '>'"$@"
	@mkdir -p "`dirname "$@"`"
	@{ sym="`echo "$<" | $(symbolname)`";                                 \
	   echo "static const char $${sym}Start[] =";                         \
	   od -vb "$<" | sed 's/[0-7]*/"/;s/ *$$/"/;/^""$$/d;s/  */\\/g';     \
	   echo ';';                                                          \
	   echo "static const int $${sym}Size ATTR_UNUSED ="                  \
	           "(int)sizeof($${sym}Start);";                              \
	 } >"$@"

