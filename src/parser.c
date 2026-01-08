#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_BUF_SIZE 4096

static char *find_crlf(const char *s, size_t len) {
  for (size_t i = 0; i + 1 < len; i++) {
    if (s[i] == '\r' && s[i + 1] == '\n') {
      return (char *)(s + i);
    }
  }
  return NULL;
}

static int parse_integer(const char *s, size_t len, long long *val) {
  long long result = 0;
  int negative = 0;
  size_t i = 0;

  if (len == 0)
    return REDIS_ERR;

  if (s[0] == '-') {
    negative = 1;
    i = 1;
  }

  for (; i < len; i++) {
    if (s[i] < '0' || s[i] > '9')
      return REDIS_ERR;
    result = result * 10 + (s[i] - '0');
  }

  *val = negative ? -result : result;
  return REDIS_OK;
}

redis_reader *redis_reader_create(void) {
  redis_reader *r = malloc(sizeof(redis_reader));
  if (!r)
    return NULL;

  r->buf = malloc(INITIAL_BUF_SIZE);
  if (!r->buf) {
    free(r);
    return NULL;
  }

  r->pos = 0;
  r->len = 0;
  r->cap = INITIAL_BUF_SIZE;
  return r;
}

void redis_reader_free(redis_reader *r) {
  if (!r)
    return;
  free(r->buf);
  free(r);
}

int redis_reader_feed(redis_reader *r, const char *buf, size_t len) {
  if (!r || !buf)
    return REDIS_ERR;

  if (r->pos > 0) {
    memmove(r->buf, r->buf + r->pos, r->len - r->pos);
    r->len -= r->pos;
    r->pos = 0;
  }

  if (r->len + len > r->cap) {
    size_t new_cap = r->cap * 2;
    while (new_cap < r->len + len)
      new_cap *= 2;

    char *new_buf = realloc(r->buf, new_cap);
    if (!new_buf)
      return REDIS_ERR;

    r->buf = new_buf;
    r->cap = new_cap;
  }

  memcpy(r->buf + r->len, buf, len);
  r->len += len;
  return REDIS_OK;
}

void redis_reply_free(redis_reply *reply) {
  if (!reply)
    return;

  if (reply->str) {
    free(reply->str);
  }

  if (reply->elements) {
    for (size_t i = 0; i < reply->elements_count; i++) {
      redis_reply_free(reply->elements[i]);
    }
    free(reply->elements);
  }

  free(reply);
}

static redis_reply *create_reply(redis_reply_type type) {
  redis_reply *reply = malloc(sizeof(redis_reply));
  if (!reply)
    return NULL;

  reply->type = type;
  reply->integer = 0;
  reply->len = 0;
  reply->str = NULL;
  reply->elements = NULL;
  reply->elements_count = 0;
  return reply;
}

static int parse_reply(redis_reader *r, redis_reply **reply);

static int parse_simple_string(redis_reader *r, redis_reply **reply,
                               redis_reply_type type) {
  char *start = r->buf + r->pos;
  size_t remaining = r->len - r->pos;

  char *crlf = find_crlf(start, remaining);
  if (!crlf)
    return REDIS_INCOMPLETE;

  size_t str_len = crlf - start;

  redis_reply *rep = create_reply(type);
  if (!rep)
    return REDIS_ERR;

  rep->str = malloc(str_len + 1);
  if (!rep->str) {
    redis_reply_free(rep);
    return REDIS_ERR;
  }

  memcpy(rep->str, start, str_len);
  rep->str[str_len] = '\0';
  rep->len = str_len;

  r->pos += str_len + 2;
  *reply = rep;
  return REDIS_OK;
}

static int parse_integer_reply(redis_reader *r, redis_reply **reply) {
  char *start = r->buf + r->pos;
  size_t remaining = r->len - r->pos;

  char *crlf = find_crlf(start, remaining);
  if (!crlf)
    return REDIS_INCOMPLETE;

  size_t num_len = crlf - start;
  long long val;

  if (parse_integer(start, num_len, &val) != REDIS_OK) {
    return REDIS_ERR;
  }

  redis_reply *rep = create_reply(REDIS_REPLY_INTEGER);
  if (!rep)
    return REDIS_ERR;

  rep->integer = val;
  r->pos += num_len + 2;
  *reply = rep;
  return REDIS_OK;
}

static int parse_bulk_string(redis_reader *r, redis_reply **reply) {
  char *start = r->buf + r->pos;
  size_t remaining = r->len - r->pos;

  char *crlf = find_crlf(start, remaining);
  if (!crlf)
    return REDIS_INCOMPLETE;

  size_t len_chars = crlf - start;
  long long str_len;

  if (parse_integer(start, len_chars, &str_len) != REDIS_OK) {
    return REDIS_ERR;
  }

  if (str_len == -1) {
    redis_reply *rep = create_reply(REDIS_REPLY_NIL);
    if (!rep)
      return REDIS_ERR;

    r->pos += len_chars + 2;
    *reply = rep;
    return REDIS_OK;
  }

  size_t header_len = len_chars + 2;
  size_t total_needed = header_len + str_len + 2;

  if (remaining < total_needed)
    return REDIS_INCOMPLETE;

  redis_reply *rep = create_reply(REDIS_REPLY_STRING);
  if (!rep)
    return REDIS_ERR;

  rep->str = malloc(str_len + 1);
  if (!rep->str) {
    redis_reply_free(rep);
    return REDIS_ERR;
  }

  memcpy(rep->str, start + header_len, str_len);
  rep->str[str_len] = '\0';
  rep->len = str_len;

  r->pos += total_needed;
  *reply = rep;
  return REDIS_OK;
}

static int parse_array(redis_reader *r, redis_reply **reply) {
  char *start = r->buf + r->pos;
  size_t remaining = r->len - r->pos;

  char *crlf = find_crlf(start, remaining);
  if (!crlf)
    return REDIS_INCOMPLETE;

  size_t len_chars = crlf - start;
  long long array_len;

  if (parse_integer(start, len_chars, &array_len) != REDIS_OK) {
    return REDIS_ERR;
  }

  r->pos += len_chars + 2;

  if (array_len == -1) {
    redis_reply *rep = create_reply(REDIS_REPLY_NIL);
    if (!rep)
      return REDIS_ERR;
    *reply = rep;
    return REDIS_OK;
  }

  redis_reply *rep = create_reply(REDIS_REPLY_ARRAY);
  if (!rep)
    return REDIS_ERR;

  if (array_len == 0) {
    *reply = rep;
    return REDIS_OK;
  }

  rep->elements = malloc(sizeof(redis_reply *) * array_len);
  if (!rep->elements) {
    redis_reply_free(rep);
    return REDIS_ERR;
  }

  for (long long i = 0; i < array_len; i++) {
    redis_reply *elem = NULL;
    int status = parse_reply(r, &elem);

    if (status != REDIS_OK) {
      rep->elements_count = i;
      redis_reply_free(rep);
      return status;
    }

    rep->elements[i] = elem;
    rep->elements_count = i + 1;
  }

  rep->len = array_len;
  *reply = rep;
  return REDIS_OK;
}

static int parse_reply(redis_reader *r, redis_reply **reply) {
  if (r->pos >= r->len)
    return REDIS_INCOMPLETE;

  char type = r->buf[r->pos];
  r->pos++;

  switch (type) {
  case '+':
    return parse_simple_string(r, reply, REDIS_REPLY_STATUS);
  case '-':
    return parse_simple_string(r, reply, REDIS_REPLY_ERROR);
  case ':':
    return parse_integer_reply(r, reply);
  case '$':
    return parse_bulk_string(r, reply);
  case '*':
    return parse_array(r, reply);
  default:
    return REDIS_ERR;
  }
}

int redis_reader_get_reply(redis_reader *r, redis_reply **reply) {
  if (!r || !reply)
    return REDIS_ERR;

  *reply = NULL;

  if (r->pos >= r->len)
    return REDIS_OK;

  size_t saved_pos = r->pos;
  int status = parse_reply(r, reply);

  if (status == REDIS_INCOMPLETE) {
    r->pos = saved_pos;
    *reply = NULL;
    return REDIS_OK;
  }

  return status;
}
