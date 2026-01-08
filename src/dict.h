#ifndef DICT_H
#define DICT_H

#include <stddef.h>

typedef struct dict_entry {
  char *key;
  char *val;
  struct dict_entry *next;
} dict_entry;

typedef struct dict {
  dict_entry **buckets;
  size_t size;
  size_t count;
} dict;

dict *dict_create(void);
int dict_set(dict *d, const char *key, const char *val);
char *dict_get(dict *d, const char *key);
void dict_free(dict *d);

#endif
