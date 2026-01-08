#include "dict.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_SIZE 1024

static unsigned long hash(const char *str) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;
  return hash;
}

dict *dict_create(void) {
  dict *d = malloc(sizeof(dict));
  if (!d)
    return NULL;

  d->buckets = calloc(INITIAL_SIZE, sizeof(dict_entry *));
  if (!d->buckets) {
    free(d);
    return NULL;
  }

  d->size = INITIAL_SIZE;
  d->count = 0;
  return d;
}

int dict_set(dict *d, const char *key, const char *val) {
  unsigned long idx = hash(key) % d->size;
  dict_entry *entry = d->buckets[idx];

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      free(entry->val);
      entry->val = strdup(val);
      return entry->val ? 1 : 0;
    }
    entry = entry->next;
  }

  entry = malloc(sizeof(dict_entry));
  if (!entry)
    return 0;

  entry->key = strdup(key);
  entry->val = strdup(val);
  if (!entry->key || !entry->val) {
    free(entry->key);
    free(entry->val);
    free(entry);
    return 0;
  }

  entry->next = d->buckets[idx];
  d->buckets[idx] = entry;
  d->count++;

  return 1;
}

char *dict_get(dict *d, const char *key) {
  unsigned long idx = hash(key) % d->size;
  dict_entry *entry = d->buckets[idx];

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      return entry->val;
    }
    entry = entry->next;
  }
  return NULL;
}

void dict_free(dict *d) {
  if (!d)
    return;
  for (size_t i = 0; i < d->size; i++) {
    dict_entry *entry = d->buckets[i];
    while (entry) {
      dict_entry *next = entry->next;
      free(entry->key);
      free(entry->val);
      free(entry);
      entry = next;
    }
  }
  free(d->buckets);
  free(d);
}
