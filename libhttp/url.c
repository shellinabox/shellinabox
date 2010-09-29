// url.c -- Object representing uniform resource locators
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

#include "config.h"

#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STRINGS_H
#include <strings.h> // for strncasecmp()
#endif

#include "libhttp/url.h"

#include "logging/logging.h"

#ifdef HAVE_UNUSED
#defined ATTR_UNUSED __attribute__((unused))
#defined UNUSED(x)   do { } while (0)
#else
#define ATTR_UNUSED
#define UNUSED(x)    do { (void)(x); } while (0)
#endif

static char *urlUnescape(char *s) {
  int warned    = 0;
  char *r       = s;
  for (char *u  = s; *u; ) {
    char ch     = *u++;
    if (ch == '+') {
      ch        = ' ';
    } else if (ch == '%') {
      char c1   = *u;
      if ((c1 >= '0' && c1 <= '9') || ((c1 &= ~0x20) >= 'A' && c1 <= 'F')) {
        ch      = c1 - (c1 > '9' ? 'A' - 10 : '0');
        char c2 = *++u;
        if ((c2 >= '0' && c2 <= '9') || ((c2 &= ~0x20) >= 'A' && c2 <= 'F')) {
          ch    = (ch << 4) + c2 - (c2 > '9' ? 'A' - 10 : '0');
          ++u;
        } else if (!warned++) {
          warn("Malformed URL encoded data \"%s\"", r);
        }
      } else if (!warned++) {
        warn("Malformed URL encoded data \"%s\"", r);
      }
    }
    *s++        = ch;
  }
  *s            = '\000';
  return r;
}

static void urlDestroyHashMapEntry(void *arg ATTR_UNUSED, char *key,
                                   char *value) {
  UNUSED(arg);
  free(key);
  free(value);
}

static char *urlMakeString(const char *buf, int len) {
  if (!buf) {
    return NULL;
  } else {
    char *s;
    check(s = malloc(len + 1));
    memcpy(s, buf, len);
    s[len]  = '\000';
    return s;
  }
}

static void urlParseQueryString(struct URL *url, const char *query, int len) {
  const char *key   = query;
  const char *value = NULL;
  for (const char *ampersand = query; len-- >= 0; ampersand++) {
    char ch         = len >= 0 ? *ampersand : '\000';
    if (ch == '=' && !value) {
      value         = ampersand + 1;
    } else if (ch == '&' || len < 0) {
      int kl        = (value ? value-1 : ampersand) - key;
      int vl        = value ? ampersand - value : 0;
      if (kl) {
        char *k     = urlMakeString(key, kl);
        urlUnescape(k);
        char *v     = NULL;
        if (value) {
          v         = urlMakeString(value, vl);
          urlUnescape(v);
        }
        addToHashMap(&url->args, k, v);
      }
      key           = ampersand + 1;
      value         = NULL;
    }
    if (!ch) {
      break;
    }
  }
}

static void urlParseHeaderLine(struct HashMap *hashmap, const char *s,
                               int len) {
  while (s && len > 0) {
    while (len > 0 && (*s == ' ' || *s == ';')) {
      s++;
      len--;
    }
    const char *key   = s;
    const char *value = NULL;
    while (len > 0 && *s != ';') {
      if (*s == '=' && value == NULL) {
        value         = s + 1;
      }
      s++;
      len--;
    }
    int kl            = (value ? value-1 : s) - key;
    int vl            = value ? s - value : 0;
    if (kl) {
      char *k         = urlMakeString(key, kl);
      for (char *t = k; *t; t++) {
        if (*t >= 'a' && *t <= 'z') {
          *t         |= 0x20;
        }
      }
      char *v         = NULL;
      if (value) {
        if (vl >= 2 && value[0] == '"' && value[vl-1] == '"') {
          value++;
          vl--;
        }
        v             = urlMakeString(value, vl);
      }
      addToHashMap(hashmap, k, v);
    }
  }
}

static const char *urlMemstr(const char *buf, int len, const char *s) {
  int sLen        = strlen(s);
  if (!sLen) {
    return buf;
  }
  while (len >= sLen) {
    if (len > sLen) {
      char *first = memchr(buf, *s, len - sLen);
      if (!first) {
        return NULL;
      }
      len        -= first - buf;
      buf         = first;
    }
    if (!memcmp(buf, s, sLen)) {
      return buf;
    }
    buf++;
    len--;
  }
  return NULL;
}

static int urlMemcmp(const char *buf, int len, const char *s) {
  int sLen = strlen(s);
  if (len < sLen) {
    return s[len];
  } else {
    return memcmp(buf, s, sLen);
  }
}

static int urlMemcasecmp(const char *buf, int len, const char *s) {
  int sLen = strlen(s);
  if (len < sLen) {
    return s[len];
  } else {
    return strncasecmp(buf, s, sLen);
  }
}

static void urlParsePart(struct URL *url, const char *buf, int len) {
  // Most browsers seem to forget quoting data in the header fields. This
  // means, it is quite possible for an HTML form to cause the submission of
  // unparseable "multipart/form-data". If this happens, we just give up
  // and ignore the malformed data.
  // Example:
  // <form method="POST" enctype="multipart/form-data">
  //   <input type="file" name="&quot;&#13;&#10;X: x=&quot;">
  //   <input type="submit">
  // </form>
  char *name           = NULL;
  for (const char *eol; !!(eol = urlMemstr(buf, len, "\r\n")); ) {
    if (buf == eol) {
      buf             += 2;
      len             -= 2;
      if (name) {
        char *value    = len ? urlMakeString(buf, len) : NULL;
        addToHashMap(&url->args, name, value);
        name           = NULL;
      }
      break;
    } else {
      if (!name && !urlMemcasecmp(buf, len, "content-disposition:")) {
        struct HashMap fields;
        initHashMap(&fields, urlDestroyHashMapEntry, NULL);
        urlParseHeaderLine(&fields, buf + 20, eol - buf - 20);
        if (getRefFromHashMap(&fields, "form-data")) {
          // We currently don't bother to deal with binary files (e.g. files
          // that include NUL characters). If this ever becomes necessary,
          // we could check for the existence of a "filename" field and use
          // that as an indicator to store the payload in something other
          // than "url->args".
          name         = (char *)getFromHashMap(&fields, "name");
          if (name && *name) {
            check(name = strdup(name));
          }
        }
        destroyHashMap(&fields);
      }
      len             -= eol - buf + 2;
      buf              = eol + 2;
    }
  }
  free(name);
}

static void urlParsePostBody(struct URL *url,
                             const struct HttpConnection *http,
                             const char *buf, int len) {
  struct HashMap contentType;
  initHashMap(&contentType, urlDestroyHashMapEntry, NULL);
  const char *ctHeader     = getFromHashMap(&http->header, "content-type");
  urlParseHeaderLine(&contentType, ctHeader, ctHeader ? strlen(ctHeader) : 0);
  if (getRefFromHashMap(&contentType, "application/x-www-form-urlencoded")) {
    urlParseQueryString(url, buf, len);
  } else if (getRefFromHashMap(&contentType, "multipart/form-data")) {
    const char *boundary   = getFromHashMap(&contentType, "boundary");
    if (boundary && *boundary) {
      const char *lastPart = NULL;
      for (const char *part = buf; len > 0; ) {
        const char *ptr;
        if ((part == buf && (ptr = urlMemstr(part, len, "--")) != NULL) ||
            (ptr = urlMemstr(part, len, "\r\n--")) != NULL) {
          len             -= ptr - part + (part == buf ? 2 : 4);
          part             = ptr + (part == buf ? 2 : 4);
          if (!urlMemcmp(part, len, boundary)) {
            int i          = strlen(boundary);
            len           -= i;
            part          += i;
            if (!urlMemcmp(part, len, "\r\n")) {
              len         -= 2;
              part        += 2;
              if (lastPart) {
                urlParsePart(url, lastPart, ptr - lastPart);
              } else {
                if (ptr != buf) {
                  info("Ignoring prologue before \"multipart/form-data\"");
                }
              }
              lastPart     = part;
            } else if (!urlMemcmp(part, len, "--\r\n")) {
              len         -= 4;
              part        += 4;
              urlParsePart(url, lastPart, ptr - lastPart);
              lastPart     = NULL;
              if (len > 0) {
                info("Ignoring epilogue past end of \"multipart/"
                     "form-data\"");
              }
            }
          }
        }
      }
      if (lastPart) {
        warn("Missing final \"boundary\" for \"multipart/form-data\"");
      }
    } else {
      warn("Missing \"boundary\" information for \"multipart/form-data\"");
    }
  }
  destroyHashMap(&contentType);
}

struct URL *newURL(const struct HttpConnection *http,
                   const char *buf, int len) {
  struct URL *url;
  check(url = malloc(sizeof(struct URL)));
  initURL(url, http, buf, len);
  return url;
}

void initURL(struct URL *url, const struct HttpConnection *http,
             const char *buf, int len) {
  url->protocol              = strdup(httpGetProtocol(http));
  url->user                  = NULL;
  url->password              = NULL;
  url->host                  = strdup(httpGetHost(http));
  url->port                  = httpGetPort(http);
  url->path                  = strdup(httpGetPath(http));
  url->pathinfo              = strdup(httpGetPathInfo(http));
  url->query                 = strdup(httpGetQuery(http));
  url->anchor                = NULL;
  url->url                   = NULL;
  initHashMap(&url->args, urlDestroyHashMapEntry, NULL);
  if (!strcmp(http->method, "GET")) {
    urlParseQueryString(url, url->query, strlen(url->query));
  } else if (!strcmp(http->method, "POST")) {
    urlParsePostBody(url, http, buf, len);
  }
}

void destroyURL(struct URL *url) {
  if (url) {
    free(url->protocol);
    free(url->user);
    free(url->password);
    free(url->host);
    free(url->path);
    free(url->pathinfo);
    free(url->query);
    free(url->anchor);
    free(url->url);
    destroyHashMap(&url->args);
  }
}

void deleteURL(struct URL *url) {
  destroyURL(url);
  free(url);
}

const char *urlGetProtocol(struct URL *url) {
  return url->protocol;
}

const char *urlGetUser(struct URL *url) {
  return url->user;
}

const char *urlGetPassword(struct URL *url) {
  return url->password;
}

const char *urlGetHost(struct URL *url) {
  return url->host;
}

int urlGetPort(struct URL *url) {
  return url->port;
}

const char *urlGetPath(struct URL *url) {
  return url->path;
}

const char *urlGetPathInfo(struct URL *url) {
  return url->pathinfo;
}

const char *urlGetQuery(struct URL *url) {
  return url->query;
}

const char *urlGetAnchor(struct URL *url) {
  return url->anchor;
}

const char *urlGetURL(struct URL *url) {
  if (!url->url) {
    const char *host           = urlGetHost(url);
    int s_size                 = 8 + strlen(host) + 25 + strlen(url->path);
    check(*(char **)&url->url  = malloc(s_size + 1));
    *url->url                  = '\000';
    strncat(url->url, url->protocol, s_size);
    strncat(url->url, "://", s_size);
    strncat(url->url, host, s_size);
    if (url->port != (strcmp(url->protocol, "http") ? 443 : 80)) {
      snprintf(strrchr(url->url, '\000'), 25, ":%d", url->port);
    }
    strncat(url->url, url->path, s_size);
  }
  return url->url;
}

const struct HashMap *urlGetArgs(struct URL *url) {
  return &url->args;
}
