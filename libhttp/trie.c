// trie.c -- Basic implementation of a trie abstract data type
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

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "libhttp/trie.h"
#include "logging/logging.h"

struct Trie *newTrie(void (*destructor)(void *, char *), void *arg) {
  struct Trie *trie;
  check(trie = malloc(sizeof(struct Trie)));
  initTrie(trie, destructor, arg);
  return trie;
}

void initTrie(struct Trie *trie, void (*destructor)(void *, char *),
              void *arg) {
  trie->destructor  = destructor;
  trie->arg         = arg;
  trie->key         = NULL;
  trie->value       = NULL;
  trie->idx         = -1;
  trie->ch          = '\000';
  trie->children    = NULL;
  trie->numChildren = 0;
}

void destroyTrie(struct Trie *trie) {
  if (trie) {
    free(trie->key);
    for (int i = 0; i < trie->numChildren; i++) {
      if (trie->destructor && !trie->children[i].ch) {
        trie->destructor(trie->arg, trie->children[i].value);
      } else {
        check(!trie->children[i].value);
      }
      destroyTrie(&trie->children[i]);
    }
    free(trie->children);
  }
}

void deleteTrie(struct Trie *trie) {
  destroyTrie(trie);
  free(trie);
}

static void addLeafToTrie(struct Trie *trie, char ch, const char *key, int len,
                          void *value) {
  check (len >= 0);
  if (len) {
    check(trie->key     = malloc(len));
    memcpy(trie->key, key, len);
  } else {
    trie->key           = NULL;
  }
  trie->value           = NULL;
  trie->idx             = len;
  trie->ch              = ch;
  check(trie->children  = malloc(sizeof(struct Trie)));
  trie->numChildren     = 1;
  initTrie(trie->children, trie->destructor, trie->arg);
  trie->children->value = value;
}

void addToTrie(struct Trie *trie, const char *key, char *value) {
  if (trie->numChildren == 0) {
    addLeafToTrie(trie, '\000', key, strlen(key), value);
  } else {
 nextNode:;
    int len                       = strlen(key);
    for (int i = 0; i < trie->idx; i++) {
      if (key[i] != trie->key[i]) {
        struct Trie *child;
        check(child               = malloc(2*sizeof(struct Trie)));
        child->destructor         = trie->destructor;
        child->arg                = trie->arg;
        check(child->key          = malloc(trie->idx - i - 1));
        memcpy(child->key, trie->key + i + 1, trie->idx - i - 1);
        child->value              = trie->value;
        child->idx                = trie->idx - i - 1;
        child->ch                 = trie->key[i];
        child->children           = trie->children;
        child->numChildren        = trie->numChildren;
        trie->value               = NULL;
        trie->idx                 = i;
        trie->children            = child;
        trie->numChildren         = 2;
        child++;
        child->destructor         = trie->destructor;
        child->arg                = trie->arg;
        if (key[i]) {
          addLeafToTrie(child, key[i], key + i + 1, len - i - 1, value);
        } else {
          initTrie(child, trie->destructor, trie->arg);
          child->value            = value;
        }
        return;
      }
    }
    for (int i = 0; i < trie->numChildren; i++) {
      if (key[trie->idx] == trie->children[i].ch) {
        if (trie->children[i].ch) {
          key                    += trie->idx + 1;
          trie                    = &trie->children[i];
          goto nextNode;
        } else {
          if (trie->destructor) {
            trie->destructor(trie->arg, trie->children[i].value);
          }
          trie->children[i].value = value;
          return;
        }
      }
    }
    key                          += trie->idx;
    len                          -= trie->idx;
    check(trie->children          = realloc(
                     trie->children, ++trie->numChildren*sizeof(struct Trie)));
    struct Trie *newNode          = &trie->children[trie->numChildren-1];
    if (*key) {
      newNode->destructor         = trie->destructor;
      newNode->arg                = trie->arg;
      addLeafToTrie(newNode, *key, key + 1, len - 1, value);
    } else {
      initTrie(newNode, trie->destructor, trie->arg);
      newNode->value              = value;
    }
  }
}

char *getFromTrie(const struct Trie *trie, const char *key, char **diff) {
  if (diff) {
    *diff              = NULL;
  }
  struct Trie *partial = NULL;
  char *partialKey     = NULL;
  for (;;) {
    if (trie->idx > 0) {
      if (memcmp(trie->key, key, trie->idx)) {
        if (diff && partial != NULL) {
          *diff        = partialKey;
          return partial->value;
        }
        return NULL;
      }
      key             += trie->idx;
    }
    for (int i = 0; ; i++) {
      if (i >= trie->numChildren) {
        if (diff && partial != NULL) {
          // If the caller provided a "diff" pointer, then we allow partial
          // matches for the longest possible prefix that is a key in the
          // trie. Upon return, the "diff" pointer points to the first
          // character in the key does not match.
          *diff        = partialKey;
          return partial->value;
        }
        return NULL;
      } else if (*key == trie->children[i].ch) {
        if (!*key) {
          if (diff) {
            *diff      = (char *)key;
          }
          return trie->children[i].value;
        }
        for (int j = i + 1; j < trie->numChildren; j++) {
          if (!trie->children[j].ch) {
            partial    = trie->children + j;
            partialKey = (char *)key;
            break;
          }
        }
        trie = &trie->children[i];
        key++;
        break;
      } else if (!trie->children[i].ch) {
        partial        = trie->children + i;
        partialKey     = (char *)key;
      }
    }
  }
}
