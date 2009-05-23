// externalfile.h -- Serve static files through HTTP/HTTPS
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
#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "shellinabox/externalfile.h"
#include "shellinabox/service.h"
#include "shellinabox/session.h"
#include "libhttp/server.h"
#include "logging/logging.h"

#ifdef HAVE_STRLCAT
#define strncat(a,b,c) ({ char *_a = (a); strlcat(_a, (b), (c)+1); _a; })
#endif

static int externalFileHttpHandler(HttpConnection *http, void *arg,
                                   const char *buf, int len) {
  checkGraveyard();
  struct ExternalFileState *state
                           = (struct ExternalFileState *)httpGetPrivate(http);
  if (!state) {
    // Normalize the path info
    URL *url               = newURL(http, buf, len);
    const char *pathInfo   = urlGetPathInfo(url);
    while (*pathInfo == '/') {
      pathInfo++;
    }
  
    // Compute file name of external file
    char *fn;
    int s_size             = strlen((char *)arg) +
                             (*pathInfo ? strlen(pathInfo) + 1 : 0);
    check(fn               = malloc(s_size + 1));
    *fn                    = '\000';
    strncat(fn, (char *)arg, s_size);
    if (*pathInfo) {
      // Append pathInfo, if available
      strncat(fn, "/", s_size);
      const char *ptr      = pathInfo;
      while (*ptr == '/') {
        ptr++;
      }
      strncat(fn, ptr, s_size);
  
      // Any files/directories starting with a dot are inaccessible to us
      do {
        if (*ptr == '.') {
          deleteURL(url);
          free(fn);
          httpSendReply(http, 404, "File not found", NO_MSG);
          return HTTP_DONE;
        }
        ptr                = strchr(ptr + 1, '/');
      } while (ptr);
    }
  
    // Open file for reading
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
    int fd                 = NOINTR(open(fn, O_RDONLY|O_LARGEFILE));

    // Recognize a couple of common MIME types
    static const struct {
      const char *suffix, *mimeType;
    } mimeTypes[]          = { { "html", "text/html; charset=utf-8" },
                               { "txt",  "text/plain; charset=utf-8" },
                               { "js",   "text/javascript; charset=utf-8" },
                               { "css",  "text/css; charset=utf-8" },
                               { "ico",  "image/x-icon" },
                               { "jpg",  "image/jpeg" },
                               { "gif",  "image/gif" },
                               { "png",  "image/png" },
                               { "wav",  "audio/x-wav" },
                               { "mp3",  "audio/mpeg" },
                               { "au",   "audio/basic" },
                               { "mid",  "audio/midi" },
                               { NULL,   NULL } };
    const char *mimeType   = "application/octet-stream";
    char *suffix           = strrchr(fn, '.');
    if (!suffix) {
      suffix               = strrchr(urlGetPath(url), '.');
    }
    if (suffix) {
      suffix++;
      for (int i = 0; mimeTypes[i].suffix; i++) {
        if (!strcmp(suffix, mimeTypes[i].suffix)) {
          mimeType         = mimeTypes[i].mimeType;
          break;
        }
      }
    }
    deleteURL(url);

    if (fd < 0) {
      free(fn);
      httpSendReply(http, 404, "File not found", NO_MSG);
      return HTTP_DONE;
    }
  
    // We only serve regular files, and restrict the file size to 100MB.
    // As a special-case, we also allow access to /dev/null.
    struct stat sb = { 0 };
    if (strcmp(fn, "/dev/null") &&
        (fstat(fd, &sb) ||
         !S_ISREG(sb.st_mode) ||
         sb.st_size > (100 << 20))) {
      free(fn);
      NOINTR(close(fd));
      httpSendReply(http, 404, "File not found", NO_MSG);
      return HTTP_DONE;
    }
    free(fn);
  
    // Set up response header
    char *response         = stringPrintf(NULL,
                                          "HTTP/1.1 200 OK\r\n"
                                          "Content-Type: %s\r\n"
                                          "Content-Length: %d\r\n"
                                          "\r\n",
                                          mimeType, (int)sb.st_size);
    int respLen            = strlen(response);

    ssize_t bytes          = -1;
    if (strcmp(httpGetMethod(http), "HEAD")) {
      // Read at most 64kB in one go
      ssize_t dataLen      = 65536;
      if (dataLen > sb.st_size) {
        dataLen            = sb.st_size;
      }
      check(response       = realloc(response, respLen + dataLen + 1));
      bytes                = NOINTR(read(fd, response + respLen, dataLen));
      
      if (bytes < 0) {
        free(response);
        NOINTR(close(fd));
        httpSendReply(http, 404, "File not found", NO_MSG);
        return HTTP_DONE;
      }
    }
  
    if (bytes < 0 || bytes == sb.st_size) {
      // Read entire file. Transmit it in one go.
      httpTransfer(http, response,
                   respLen + (bytes > 0 ? bytes : 0));
      NOINTR(close(fd));
      return HTTP_DONE;
    } else {
      // Transmit partial reply and store state for future calls into the
      // handler.
      httpTransferPartialReply(http, response, respLen + bytes);
      
      check(state          = malloc(sizeof(struct ExternalFileState)));
      state->fd            = fd;
      state->totalSize     = sb.st_size;
      state->partialSize   = bytes;
      httpSetPrivate(http, state);

      return HTTP_PARTIAL_REPLY;
    }
  } else {
    // We get called again, because all previously read partial data has now
    // been sent to the peer.
    int rc                 = HTTP_DONE;

    // If the connection was closed unexpectedly, clean up now
    if (!buf) {
    done:
      NOINTR(close(state->fd));
      free(state);
      httpSetPrivate(http, NULL);
      return rc;
    } else {
      ssize_t dataLen      = 65536;
      if (dataLen > state->totalSize - state->partialSize) {
        dataLen            = state->totalSize - state->partialSize;
      }
      char *buf;
      check(buf            = malloc(dataLen));
      ssize_t bytes        = NOINTR(read(state->fd, buf, dataLen));
      if (bytes < 0) {
        free(buf);
        rc                 = HTTP_ERROR;
        goto done;
      }
      state->partialSize  += bytes;
      if (state->partialSize >= state->totalSize) {
        // Done serving the entire file
        httpTransfer(http, buf, bytes);
        goto done;
      } else {
        // More partial data pending
        httpTransferPartialReply(http, buf, bytes);
        return HTTP_PARTIAL_REPLY;
      }
    }
  }
}

int registerExternalFiles(void *arg, const char *key, char **value) {
  Server *server = (Server *)arg;
  if (*key == '/') {
    // Absolute URL paths get registered for this particular path, only
    serverRegisterHttpHandler(server, key, externalFileHttpHandler, *value);
  } else {
    // Relative URL paths get registered for each of the services
    for (int i = 0; i < numServices; i++) {
      char *path;
      int s_size = strlen(services[i]->path) + strlen(key) + 1;
      check(path = malloc(s_size + 1));
      *path      = '\000';
      strncat(path, services[i]->path, s_size);
      if (!*services[i]->path ||
          strrchr(services[i]->path, '\000')[-1] != '/') {
        strncat(path, "/", s_size);
      }
      strncat(path, key, s_size);
      serverRegisterHttpHandler(server, path, externalFileHttpHandler, *value);
      free(path);
    }
  }
  return 1;
}
