AM_CPPFLAGS          =
AM_CFLAGS            = -g -std=gnu99 -Wall
AM_LDFLAGS           = -g
ACLOCAL_AMFLAGS      = -I m4

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
                       TODO
EXTRA_DIST           = demo/beep.wav                                          \
                       demo/favicon.ico                                       \
                       demo/demo.html                                         \
                       demo/demo.js                                           \
                       demo/demo.jspp                                         \
                       demo/demo.xml                                          \
                       demo/styles.css                                        \
                       demo/vt100.js                                          \
                       shellinabox/shellinaboxd.man.in                        \
                       shellinabox/shell_in_a_box.js                          \
                       shellinabox/vt100.js                                   \
                       debian/README                                          \
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
                       debian/shellinabox.postrm
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
                       shellinabox/cgi_root.html                              \
                       shellinabox/root_page.html                             \
                       shellinabox/vt100.jspp                                 \
                       shellinabox/shell_in_a_box.jspp                        \
                       shellinabox/styles.css                                 \
                       shellinabox/favicon.ico                                \
                       shellinabox/beep.wav                                   \
                       config.h
shellinaboxd_LDADD   = liblogging.la                                          \
                       libhttp.la
shellinaboxd_LDFLAGS = -static

objcopyflags         = case "$(host_cpu)" in                                  \
                         i[0-9]86) echo '-O elf32-i386 -B i386';;             \
                         x86_64)   echo '-O elf64-x86-64 -B i386:x86-64';;    \
                         arm*)     echo '-O elf32-littlearm -B arm';;         \
                       esac

renamesymbols        =                                                        \
  sed -e 's/\(.*\/\)\([^.]*\)\([.].*\)/\1\2\3=\2 /'                           \
      -e 't0'                                                                 \
      -e 's/\([^.]*\)\([.].*\)/\1\2=\1 /'                                     \
      -e 't0'                                                                 \
      -e 's/.*/&=& /'                                                         \
      -e ':0'                                                                 \
      -e 's/$$/aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ/'         \
      -e ':1'                                                                 \
      -e 's/\(=[^_]*\)_\([a-z]\)\([^ ]* .*\2\)\(.\)/\1\4\3\4/'                \
      -e 't1'                                                                 \
      -e 's/.\{53\}$$//'                                                      \
      -e 's/[/.]/_/g'                                                         \
      -e 's/^/--redefine-sym _binary_/'                                       \
      -e 's/\([^=]*\)\(=[^ ]*\)/& \1_end\2End/'                               \
      -e 's/\([^=]*\)\(=[^ ]*\)/& \1_start\2Start/'                           \
      -e 's/[^ ]*\([^=]*\)=[^ ]*/-N\1_size/'

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck


${top_srcdir}/demo/demo.js: ${top_srcdir}/demo/beep.wav                       \
                            ${top_srcdir}/demo/demo.jspp                      \
                            ${top_srcdir}/demo/favicon.ico                    \
                            ${top_srcdir}/demo/styles.css                     \
                            ${top_srcdir}/demo/vt100.js

${top_srcdir}/demo/beep.wav: ${top_srcdir}/shellinabox/beep.wav
	@rm -f "$@"
	ln "$<" "$@"

${top_srcdir}/demo/favicon.ico: ${top_srcdir}/shellinabox/favicon.ico
	@rm -f "$@"
	ln "$<" "$@"

${top_srcdir}/demo/styles.css: ${top_srcdir}/shellinabox/styles.css
	@rm -f "$@"
	ln "$<" "$@"

${top_srcdir}/demo/vt100.js: ${top_srcdir}/shellinabox/vt100.js
	@rm -f "$@"
	ln "$<" "$@"

shellinaboxd.1: shellinabox/shellinaboxd.man.in config.h
	@src="${top_srcdir}/shellinabox/shellinaboxd.man.in";                 \
	echo preprocess  "$$src" '>'"$@";                                     \
	if [ `sed -e 's/^#define \([^ ]*\).*/\1/' -e t -e d config.h |        \
             egrep 'HAVE_OPENSSL_BIO_H|HAVE_OPENSSL_ERR_H|HAVE_OPENSSL_SSL_H'|\
             wc -l` -eq 3 ]; then                                             \
	  sed -e '/^#ifdef  *HAVE_OPENSSL$$/d'                                \
	      -e '/^#endif$$/d' "$$src" >"$@";                                \
	else                                                                  \
	  sed -e '/^#ifdef  *HAVE_OPENSSL$$/,/^#endif$$/d' "$$src" >"$@";     \
	fi;                                                                   \
	if sed -e 's/^#define \([^ ]*\).*/\1/' -e t -e d config.h |           \
             grep 'HAVE_SECURITY_PAM_APPL_H' >/dev/null 2>&1; then            \
	  sed -e '/^#ifdef  *HAVE_PAM$$/d'                                    \
	      -e '/^#endif$$/d' "$$src" >"$@";                                \
	else                                                                  \
	  sed -e '/^#ifdef  *HAVE_PAM$$/,/^#endif$$/d' "$$src" >"$@";         \
	fi
	@out=`echo "$@" 2>/dev/null|sed -e 's/\.[^.]*$$/.ps/'`;               \
	man -Tps "./$@" >"$${out}" 2>/dev/null || rm -f "$${out}"

clean-local:
	-rm -rf shellinaboxd.1                                                \
                shellinaboxd.ps
	-rm -rf debian/shellinabox                                            \
                debian/shellinabox*.debhelper*                                \
	        debian/shellinabox.substvars                                  \
                debian/tmp

.css.o:
	@echo objcopy "$<" "$@"
	@objcopy -I binary `$(objcopyflags)` `echo "$<" | $(renamesymbols)`   \
	  "$<" "$@"

.html.o:
	@echo objcopy "$<" "$@"
	@objcopy -I binary `$(objcopyflags)` `echo "$<" | $(renamesymbols)`   \
	  "$<" "$@"

.ico.o:
	@echo objcopy "$<" "$@"
	@objcopy -I binary `$(objcopyflags)` `echo "$<" | $(renamesymbols)`   \
	  "$<" "$@"

shellinabox/shell_in_a_box.o: shellinabox/shell_in_a_box.js config.h

.jspp.js:
	@echo preprocess "$<" "$@"
	@sed -e "`sed -e 's/^#define *\([^ ]*\) *\(.*\)/\/^[^#]\/s\/\1\/\2 \\\\\/* \1 *\\\\\/\/g/' \
	             -e t                                                     \
	             -e d "$<"`"                                              \
	     -e "s/^#/\/\/ #/"                                                \
	     -e "s/VERSION/\"@VERSION@ (revision @VCS_REVISION@)\"/g"         \
	     "$<" >"$@"

.js.o:
	@echo objcopy "$<" "$@"
	@objcopy -I binary `$(objcopyflags)` `echo "$<" | $(renamesymbols)`   \
	  "$<" "$@"

.wav.o:
	@echo objcopy "$<" "$@"
	@objcopy -I binary `$(objcopyflags)` `echo "$<" | $(renamesymbols)`   \
	  "$<" "$@"

