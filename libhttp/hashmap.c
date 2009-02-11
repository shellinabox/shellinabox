// hashmap.c -- Basic implementation of a hashmap abstract data type
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

#include "libhttp/hashmap.h"
#include "logging/logging.h"

struct HashMap *newHashMap(void (*destructor)(void *arg, char *key,
                                              char *value),
                           void *arg) {
  struct HashMap *hashmap;
  check(hashmap = malloc(sizeof(struct HashMap)));
  initHashMap(hashmap, destructor, arg);
  return hashmap;
}

void initHashMap(struct HashMap *hashmap,
                 void (*destructor)(void *arg, char *key, char *value),
                 void *arg) {
  hashmap->destructor  = destructor;
  hashmap->arg         = arg;
  hashmap->entries     = NULL;
  hashmap->mapSize     = 0;
  hashmap->numEntries  = 0;
}

void destroyHashMap(struct HashMap *hashmap) {
  if (hashmap) {
    for (int i = 0; i < hashmap->mapSize; i++) {
      if (hashmap->entries[i]) {
        for (int j = 0; hashmap->entries[i][j].key; j++) {
          if (hashmap->destructor) {
            hashmap->destructor(hashmap->arg,
                                (char *)hashmap->entries[i][j].key,
                                (char *)hashmap->entries[i][j].value);
          }
        }
        free(hashmap->entries[i]);
      }
    }
    free(hashmap->entries);
  }
}

void deleteHashMap(struct HashMap *hashmap) {
  destroyHashMap(hashmap);
  free(hashmap);
}

static unsigned int stringHashFunc(const char *s) {
  unsigned int h = 0;
  while (*s) {
    h = 31*h + *(unsigned char *)s++;
  }
  return h;
}

const void *addToHashMap(struct HashMap *hashmap, const char *key,
                         const char *value) {
  if (hashmap->numEntries + 1 > (hashmap->mapSize * 8)/10) {
    struct HashMap newMap;
    newMap.numEntries            = hashmap->numEntries;
    if (hashmap->mapSize == 0) {
      newMap.mapSize             = 32;
    } else if (hashmap->mapSize < 1024) {
      newMap.mapSize             = 2*hashmap->mapSize;
    } else {
      newMap.mapSize             = hashmap->mapSize + 1024;
    }
    check(newMap.entries         = calloc(sizeof(void *), newMap.mapSize));
    for (int i = 0; i < hashmap->mapSize; i++) {
      if (!hashmap->entries[i]) {
        continue;
      }
      for (int j = 0; hashmap->entries[i][j].key; j++) {
        addToHashMap(&newMap, hashmap->entries[i][j].key,
                     hashmap->entries[i][j].value);
      }
      free(hashmap->entries[i]);
    }
    free(hashmap->entries);
    hashmap->entries             = newMap.entries;
    hashmap->mapSize             = newMap.mapSize;
    hashmap->numEntries          = newMap.numEntries;
  }
  unsigned hash                  = stringHashFunc(key);
  int idx                        = hash % hashmap->mapSize;
  int i                          = 0;
  if (hashmap->entries[idx]) {
    for (i = 0; hashmap->entries[idx][i].key; i++) {
      if (!strcmp(hashmap->entries[idx][i].key, key)) {
        if (hashmap->destructor) {
          hashmap->destructor(hashmap->arg,
                              (char *)hashmap->entries[idx][i].key,
                              (char *)hashmap->entries[idx][i].value);
        }
        hashmap->entries[idx][i].key   = key;
        hashmap->entries[idx][i].value = value;
        return value;
      }
    }
  }
  check(hashmap->entries[idx]    = realloc(hashmap->entries[idx],
                                        (i+2)*sizeof(*hashmap->entries[idx])));
  hashmap->entries[idx][i].key   = key;
  hashmap->entries[idx][i].value = value;
  memset(&hashmap->entries[idx][i+1], 0, sizeof(*hashmap->entries[idx]));
  hashmap->numEntries++;
  return value;
}

void deleteFromHashMap(struct HashMap *hashmap, const char *key) {
  if (hashmap->mapSize == 0) {
    return;
  }
  unsigned hash = stringHashFunc(key);
  int idx       = hash % hashmap->mapSize;
  if (!hashmap->entries[idx]) {
    return;
  }
  for (int i = 0; hashmap->entries[idx][i].key; i++) {
    if (!strcmp(hashmap->entries[idx][i].key, key)) {
      int j     = i + 1;
      while (hashmap->entries[idx][j].key) {
        j++;
      }
      if (hashmap->destructor) {
        hashmap->destructor(hashmap->arg,
                            (char *)hashmap->entries[idx][i].key,
                            (char *)hashmap->entries[idx][i].value);
      }
      if (i != j-1) {
        memcpy(&hashmap->entries[idx][i], &hashmap->entries[idx][j-1],
               sizeof(*hashmap->entries[idx]));
      }
      memset(&hashmap->entries[idx][j-1], 0, sizeof(*hashmap->entries[idx]));
      check(--hashmap->numEntries >= 0);
    }
  }
}

char **getRefFromHashMap(const struct HashMap *hashmap, const char *key) {
  if (hashmap->mapSize == 0) {
    return NULL;
  }
  unsigned hash = stringHashFunc(key);
  int idx       = hash % hashmap->mapSize;
  if (!hashmap->entries[idx]) {
    return NULL;
  }
  for (int i = 0; hashmap->entries[idx][i].key; i++) {
    if (!strcmp(hashmap->entries[idx][i].key, key)) {
      return (char **)&hashmap->entries[idx][i].value;
    }
  }
  return NULL;
}

const char *getFromHashMap(const struct HashMap *hashmap, const char *key) {
  char **ref = getRefFromHashMap(hashmap, key);
  return ref ? *ref : NULL;
}

void iterateOverHashMap(struct HashMap *hashmap,
                        int (*fnc)(void *arg, const char *key, char **value),
                        void *arg) {
  for (int i = 0; i < hashmap->mapSize; i++) {
    if (hashmap->entries[i]) {
      int count = 0;
      while (hashmap->entries[i][count].key) {
        count++;
      }
      for (int j = 0; j < count; j++) {
        if (!fnc(arg, hashmap->entries[i][j].key,
                 (char **)&hashmap->entries[i][j].value)) {
          if (hashmap->destructor) {
            hashmap->destructor(hashmap->arg,
                                (char *)hashmap->entries[i][j].key,
                                (char *)hashmap->entries[i][j].value);
          }
          if (j != count-1) {
            memcpy(&hashmap->entries[i][j], &hashmap->entries[i][count-1],
                   sizeof(*hashmap->entries[i]));
          }
          memset(&hashmap->entries[i][count-1], 0,
                 sizeof(*hashmap->entries[i]));
          count--;
          j--;
        }
      }
    }
  }
}

int getHashmapSize(const struct HashMap *hashmap) {
  return hashmap->numEntries;
}
