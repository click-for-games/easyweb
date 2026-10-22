/* easyweb - tiny JSON reader/writer. */
#include "ew.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char* s;
  size_t len;
  size_t pos;
  char* err;
  size_t errsz;
  bool failed;
} JParse;

static void jerr(JParse* j, const char* msg) {
  if (j->failed) return;
  j->failed = true;
  if (j->err && j->errsz) snprintf(j->err, j->errsz, "%s", msg);
}

static void jskip(JParse* j) {
  while (j->pos < j->len) {
    char c = j->s[j->pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      j->pos++;
      continue;
    }
    break;
  }
}

static Value jvalue(JParse* j);

static char* jstring_raw(JParse* j) {
  if (j->pos >= j->len || j->s[j->pos] != '"') {
    jerr(j, "expected a string");
    return NULL;
  }
  j->pos++;
  Buf b;
  buf_init(&b);
  while (j->pos < j->len) {
    char c = j->s[j->pos];
    if (c == '"') {
      j->pos++;
      return buf_release(&b);
    }
    if (c == '\\') {
      j->pos++;
      if (j->pos >= j->len) break;
      char e = j->s[j->pos++];
      switch (e) {
        case 'n': buf_putc(&b, '\n'); break;
        case 't': buf_putc(&b, '\t'); break;
        case 'r': buf_putc(&b, '\r'); break;
        case 'b': buf_putc(&b, '\b'); break;
        case 'f': buf_putc(&b, '\f'); break;
        case '/': buf_putc(&b, '/'); break;
        case '"': buf_putc(&b, '"'); break;
        case '\\': buf_putc(&b, '\\'); break;
        case 'u': {
          if (j->pos + 4 > j->len) { jerr(j, "bad unicode escape"); buf_free(&b); return NULL; }
          unsigned int cp = 0;
          for (int i = 0; i < 4; i++) {
            char h = j->s[j->pos + i];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
            else { jerr(j, "bad unicode escape"); buf_free(&b); return NULL; }
          }
          j->pos += 4;
          if (cp < 0x80) {
            buf_putc(&b, (char)cp);
          } else if (cp < 0x800) {
            buf_putc(&b, (char)(0xC0 | (cp >> 6)));
            buf_putc(&b, (char)(0x80 | (cp & 0x3F)));
          } else {
            buf_putc(&b, (char)(0xE0 | (cp >> 12)));
            buf_putc(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
            buf_putc(&b, (char)(0x80 | (cp & 0x3F)));
          }
          break;
        }
        default:
          buf_putc(&b, e);
          break;
      }
      continue;
    }
    buf_putc(&b, c);
    j->pos++;
  }
  jerr(j, "string is never closed");
  buf_free(&b);
  return NULL;
}

static Value jvalue(JParse* j) {
  jskip(j);
  if (j->pos >= j->len) {
    jerr(j, "unexpected end of json");
    return v_null();
  }
  char c = j->s[j->pos];
  if (c == '{') {
    j->pos++;
    Value obj = v_obj();
    jskip(j);
    if (j->pos < j->len && j->s[j->pos] == '}') {
      j->pos++;
      return obj;
    }
    for (;;) {
      jskip(j);
      char* key = jstring_raw(j);
      if (!key) { value_release(&obj); return v_null(); }
      jskip(j);
      if (j->pos >= j->len || j->s[j->pos] != ':') {
        jerr(j, "expected : after a name");
        free(key);
        value_release(&obj);
        return v_null();
      }
      j->pos++;
      Value v = jvalue(j);
      if (j->failed) {
        free(key);
        value_release(&obj);
        return v_null();
      }
      obj_set(&obj, key, v);
      value_release(&v);
      free(key);
      jskip(j);
      if (j->pos < j->len && j->s[j->pos] == ',') {
        j->pos++;
        continue;
      }
      break;
    }
    jskip(j);
    if (j->pos >= j->len || j->s[j->pos] != '}') {
      jerr(j, "expected }");
      value_release(&obj);
      return v_null();
    }
    j->pos++;
    return obj;
  }
  if (c == '[') {
    j->pos++;
    Value arr = arr_new();
    jskip(j);
    if (j->pos < j->len && j->s[j->pos] == ']') {
      j->pos++;
      return arr;
    }
    for (;;) {
      Value v = jvalue(j);
      if (j->failed) {
        value_release(&arr);
        return v_null();
      }
      arr_push(&arr, v);
      value_release(&v);
      jskip(j);
      if (j->pos < j->len && j->s[j->pos] == ',') {
        j->pos++;
        continue;
      }
      break;
    }
    jskip(j);
    if (j->pos >= j->len || j->s[j->pos] != ']') {
      jerr(j, "expected ]");
      value_release(&arr);
      return v_null();
    }
    j->pos++;
    return arr;
  }
  if (c == '"') {
    char* s = jstring_raw(j);
    if (!s) return v_null();
    Value v = v_str(s);
    free(s);
    return v;
  }
  if (strncmp(j->s + j->pos, "true", 4) == 0 && j->pos + 4 <= j->len) {
    j->pos += 4;
    return v_bool(true);
  }
  if (strncmp(j->s + j->pos, "false", 5) == 0 && j->pos + 5 <= j->len) {
    j->pos += 5;
    return v_bool(false);
  }
  if (strncmp(j->s + j->pos, "null", 4) == 0 && j->pos + 4 <= j->len) {
    j->pos += 4;
    return v_null();
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    char* end = NULL;
    double d = strtod(j->s + j->pos, &end);
    if (end == j->s + j->pos) {
      jerr(j, "bad number");
      return v_null();
    }
    j->pos = (size_t)(end - j->s);
    return v_num(d);
  }
  jerr(j, "unexpected character in json");
  return v_null();
}

Value json_parse(const char* text, size_t len, char* err, size_t errsz) {
  JParse j;
  memset(&j, 0, sizeof(j));
  j.s = text;
  j.len = len;
  j.err = err;
  j.errsz = errsz;
  Value v = jvalue(&j);
  if (j.failed) {
    value_release(&v);
    return v_null();
  }
  jskip(&j);
  return v;
}

char* json_escape(const char* s, size_t len) {
  Buf b;
  buf_init(&b);
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"': buf_puts(&b, "\\\""); break;
      case '\\': buf_puts(&b, "\\\\"); break;
      case '\n': buf_puts(&b, "\\n"); break;
      case '\r': buf_puts(&b, "\\r"); break;
      case '\t': buf_puts(&b, "\\t"); break;
      case '\b': buf_puts(&b, "\\b"); break;
      case '\f': buf_puts(&b, "\\f"); break;
      default:
        if (c < 0x20) buf_printf(&b, "\\u%04x", c);
        else buf_putc(&b, (char)c);
        break;
    }
  }
  return buf_release(&b);
}

static void json_write(Buf* b, Value v) {
  switch (v.k) {
    case V_NULL: buf_puts(b, "null"); break;
    case V_BOOL: buf_puts(b, v.as.b ? "true" : "false"); break;
    case V_NUM: {
      double d = v.as.n;
      if (d != d || d == (double)INFINITY || d == -(double)INFINITY) {
        buf_puts(b, "null");
      } else if (d == (double)(long long)d && d < 1e15 && d > -1e15) {
        buf_printf(b, "%lld", (long long)d);
      } else {
        buf_printf(b, "%.17g", d);
      }
      break;
    }
    case V_STR: {
      char* e = json_escape(v.as.s->data, v.as.s->len);
      buf_putc(b, '"');
      buf_puts(b, e);
      buf_putc(b, '"');
      free(e);
      break;
    }
    case V_ARR: {
      buf_putc(b, '[');
      for (size_t i = 0; i < arr_len(&v); i++) {
        if (i) buf_putc(b, ',');
        Value item = arr_get(&v, i);
        json_write(b, item);
      }
      buf_putc(b, ']');
      break;
    }
    case V_OBJ: {
      buf_putc(b, '{');
      char** keys = obj_keys(&v);
      for (int i = 0; keys[i]; i++) {
        if (i) buf_putc(b, ',');
        char* e = json_escape(keys[i], strlen(keys[i]));
        buf_putc(b, '"');
        buf_puts(b, e);
        buf_puts(b, "\":");
        free(e);
        Value val;
        if (obj_get(&v, keys[i], &val)) {
          json_write(b, val);
          value_release(&val);
        } else {
          buf_puts(b, "null");
        }
        free(keys[i]);
      }
      free(keys);
      buf_putc(b, '}');
      break;
    }
    case V_FUN:
      buf_puts(b, "null");
      break;
  }
}

char* json_from_value(Value v) {
  Buf b;
  buf_init(&b);
  json_write(&b, v);
  return buf_release(&b);
}
