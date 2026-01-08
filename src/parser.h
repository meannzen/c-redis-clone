#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

typedef enum {
  REDIS_OK = 0,
  REDIS_ERR = -1,
  REDIS_INCOMPLETE = 1
} redis_status;

typedef enum {
  REDIS_REPLY_STRING = 1,
  REDIS_REPLY_ARRAY = 2,
  REDIS_REPLY_INTEGER = 3,
  REDIS_REPLY_NIL = 4,
  REDIS_REPLY_STATUS = 5,
  REDIS_REPLY_ERROR = 6
} redis_reply_type;

typedef struct redis_reply {
  redis_reply_type type; /* REDIS_REPLY_STRING, REDIS_REPLY_ARRAY, etc. */
  long long integer;     /* For Integer type */
  size_t len;            /* Length of string/array */
  char *str;             /* For String/Error types */
  struct redis_reply **elements; /* For Array type (recursive) */
  size_t elements_count;
} redis_reply;

typedef struct redis_reader {
  char *buf;  /* Buffer containing unparsed data */
  size_t pos; /* Current reading position in buf */
  size_t len; /* Total length of data in buf */
  size_t cap; /* Buffer capacity */
} redis_reader;

redis_reader *redis_reader_create(void);
void redis_reader_free(redis_reader *r);
int redis_reader_feed(redis_reader *r, const char *buf, size_t len);
int redis_reader_get_reply(redis_reader *r, redis_reply **reply);
void redis_reply_free(redis_reply *reply);

#endif
