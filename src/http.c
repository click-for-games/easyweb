/* easyweb - HTTP request parsing and response building. */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static char* url_decode(const char* s, size_t len) {
  Buf b;
  buf_init(&b);
  for (size_t i = 0; i < len; i++) {
    if (s[i] == '%' && i + 2 < len) {
      int h = hexval(s[i + 1]), l = hexval(s[i + 2]);
      if (h >= 0 && l >= 0) {
        buf_putc(&b, (char)((h << 4) | l));
        i += 2;
        continue;
      }
    }
    if (s[i] == '+') {
      buf_putc(&b, ' ');
      continue;
    }
    buf_putc(&b, s[i]);
  }
  return buf_release(&b);
}

void http_request_free(HttpRequest* r) {
  if (!r) return;
  free(r->method);
  free(r->path);
  free(r->raw_path);
  free(r->query);
  free(r->body);
  for (int i = 0; i < r->nheaders; i++) {
    free(r->header_names[i]);
    free(r->header_values[i]);
  }
  free(r->header_names);
  free(r->header_values);
  for (int i = 0; i < r->nparams; i++) {
    free(r->param_names[i]);
    free(r->param_values[i]);
  }
  free(r->param_names);
  free(r->param_values);
  memset(r, 0, sizeof(*r));
}

void http_response_free(HttpResponse* r) {
  if (!r) return;
  free(r->content_type);
  free(r->body);
  for (int i = 0; i < r->nheaders; i++) {
    free(r->header_names[i]);
    free(r->header_values[i]);
  }
  free(r->header_names);
  free(r->header_values);
  memset(r, 0, sizeof(*r));
}

void http_response_set_header(HttpResponse* r, const char* name, const char* value) {
  for (int i = 0; i < r->nheaders; i++) {
    if (strcmp(r->header_names[i], name) == 0) {
      free(r->header_values[i]);
      r->header_values[i] = ew_strdup(value);
      return;
    }
  }
  r->header_names = (char**)ew_realloc(r->header_names, sizeof(char*) * (size_t)(r->nheaders + 1));
  r->header_values = (char**)ew_realloc(r->header_values, sizeof(char*) * (size_t)(r->nheaders + 1));
  r->header_names[r->nheaders] = ew_strdup(name);
  r->header_values[r->nheaders] = ew_strdup(value);
  r->nheaders++;
}

void http_response_send(HttpResponse* r, int status, const char* content_type, const char* body, size_t len) {
  r->status = status;
  free(r->content_type);
  r->content_type = ew_strdup(content_type ? content_type : "text/plain; charset=utf-8");
  free(r->body);
  r->body = (char*)ew_malloc(len + 1);
  if (len && body) memcpy(r->body, body, len);
  r->body[len] = 0;
  r->body_len = len;
}

static const char* status_text(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default: return "OK";
  }
}

char* http_response_render(HttpResponse* r, size_t* out_len) {
  Buf b;
  buf_init(&b);
  buf_printf(&b, "HTTP/1.1 %d %s\r\n", r->status, status_text(r->status));
  buf_printf(&b, "Content-Type: %s\r\n", r->content_type ? r->content_type : "text/plain; charset=utf-8");
  buf_printf(&b, "Content-Length: %zu\r\n", r->body_len);
  buf_printf(&b, "X-Powered-By: easyweb/%s\r\n", EW_VERSION);
  buf_puts(&b, "Connection: close\r\n");
  buf_puts(&b, "X-Content-Type-Options: nosniff\r\n");
  buf_puts(&b, "X-Frame-Options: SAMEORIGIN\r\n");
  buf_puts(&b, "Referrer-Policy: no-referrer\r\n");
  for (int i = 0; i < r->nheaders; i++) {
    buf_printf(&b, "%s: %s\r\n", r->header_names[i], r->header_values[i]);
  }
  buf_puts(&b, "\r\n");
  if (r->body && r->body_len) buf_putn(&b, r->body, r->body_len);
  if (out_len) *out_len = b.len;
  return buf_release(&b);
}

bool http_parse_request(const char* raw, size_t len, HttpRequest* out, char* err, size_t errsz) {
  memset(out, 0, sizeof(*out));
  const char* hdr_end = NULL;
  for (size_t i = 0; i + 3 < len; i++) {
    if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') {
      hdr_end = raw + i;
      break;
    }
  }
  if (!hdr_end) {
    if (err) snprintf(err, errsz, "incomplete request headers");
    return false;
  }
  size_t head_len = (size_t)(hdr_end - raw);
  const char* body_start = hdr_end + 4;
  size_t body_len = len - (size_t)(body_start - raw);

  /* request line */
  const char* eol = memchr(raw, '\r', head_len);
  if (!eol) eol = raw + head_len;
  const char* sp1 = memchr(raw, ' ', (size_t)(eol - raw));
  if (!sp1) {
    if (err) snprintf(err, errsz, "bad request line");
    return false;
  }
  const char* sp2 = memchr(sp1 + 1, ' ', (size_t)(eol - sp1 - 1));
  out->method = ew_strndup(raw, (size_t)(sp1 - raw));
  const char* target = sp1 + 1;
  size_t target_len = sp2 ? (size_t)(sp2 - target) : (size_t)(eol - target);
  out->raw_path = ew_strndup(target, target_len);

  /* split query */
  char* qmark = strchr(out->raw_path, '?');
  if (qmark) {
    *qmark = 0;
    out->query = ew_strdup(qmark + 1);
  }
  out->path = url_decode(out->raw_path, strlen(out->raw_path));

  /* headers */
  const char* hp = eol;
  while (hp < hdr_end) {
    const char* he = memchr(hp, '\r', (size_t)(hdr_end - hp));
    if (!he) break;
    if (he == hp) break;
    const char* colon = memchr(hp, ':', (size_t)(he - hp));
    if (colon) {
      char* name = ew_strndup(hp, (size_t)(colon - hp));
      const char* vs = colon + 1;
      while (vs < he && (*vs == ' ' || *vs == '\t')) vs++;
      char* value = ew_strndup(vs, (size_t)(he - vs));
      out->header_names = (char**)ew_realloc(out->header_names, sizeof(char*) * (size_t)(out->nheaders + 1));
      out->header_values = (char**)ew_realloc(out->header_values, sizeof(char*) * (size_t)(out->nheaders + 1));
      out->header_names[out->nheaders] = name;
      out->header_values[out->nheaders] = value;
      out->nheaders++;
    }
    hp = he + 2;
    while (hp < hdr_end && (*hp == ' ' || *hp == '\t')) hp++;
  }

  if (body_len > 0) {
    out->body = (char*)ew_malloc(body_len + 1);
    memcpy(out->body, body_start, body_len);
    out->body[body_len] = 0;
    out->body_len = body_len;
  }
  return true;
}
