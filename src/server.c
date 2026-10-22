/* easyweb - the built in web server.
 * Small, fast, dependency free. One socket, a thread per connection.
 */
#include "ew.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef HANDLE ew_thread_t;
#define EW_THREAD_RETURN DWORD WINAPI
#define EW_THREAD_CALL(fn, arg) CreateThread(NULL, 0, fn, arg, 0, NULL)
#define ew_close_socket closesocket
#define ew_socket SOCKET
#define EW_INVALID INVALID_SOCKET
#define EW_SHUT_BOTH SD_BOTH
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef pthread_t ew_thread_t;
#define EW_THREAD_RETURN void*
#define EW_THREAD_CALL(fn, arg) ({ pthread_t t; pthread_create(&t, NULL, fn, arg); t; })
#define ew_close_socket close
#define ew_socket int
#define EW_INVALID (-1)
#define EW_SHUT_BOTH SHUT_RDWR
#endif

#include <sys/stat.h>

#ifdef _WIN32
#define ew_strncasecmp _strnicmp
#else
#include <strings.h>
#define ew_strncasecmp strncasecmp
#endif

static volatile long g_active = 0;
static long g_max_threads = 64;

#ifdef _WIN32
static CRITICAL_SECTION g_lock;
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK init_lock_once(PINIT_ONCE o, PVOID p, PVOID* c) {
  (void)o; (void)p; (void)c;
  InitializeCriticalSection(&g_lock);
  return TRUE;
}
static void lock_init(void) {
  InitOnceExecuteOnce(&g_lock_once, init_lock_once, NULL, NULL);
}
static void lock_get(void) {
  EnterCriticalSection(&g_lock);
}
static void lock_release(void) {
  LeaveCriticalSection(&g_lock);
}
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void lock_init(void) {}
static void lock_get(void) { pthread_mutex_lock(&g_lock); }
static void lock_release(void) { pthread_mutex_unlock(&g_lock); }
#endif

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void open_browser(const char* url) {
#ifdef _WIN32
  ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
  if (system(cmd) != 0) { /* ignore */ }
#endif
}

static const char* mime_for(const char* path) {
  const char* ext = path_ext(path);
  const char* m = "application/octet-stream";
  if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) m = "text/html; charset=utf-8";
  else if (strcmp(ext, "css") == 0) m = "text/css; charset=utf-8";
  else if (strcmp(ext, "js") == 0) m = "application/javascript; charset=utf-8";
  else if (strcmp(ext, "json") == 0) m = "application/json; charset=utf-8";
  else if (strcmp(ext, "png") == 0) m = "image/png";
  else if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) m = "image/jpeg";
  else if (strcmp(ext, "gif") == 0) m = "image/gif";
  else if (strcmp(ext, "svg") == 0) m = "image/svg+xml";
  else if (strcmp(ext, "ico") == 0) m = "image/x-icon";
  else if (strcmp(ext, "txt") == 0) m = "text/plain; charset=utf-8";
  else if (strcmp(ext, "webp") == 0) m = "image/webp";
  free((void*)ext);
  return m;
}

/* ------------------------------------------------------------------ */
/* request handling                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
  ServerOpts* opts;
  char* raw;
  size_t len;
} Conn;

static void build_req_object(Interp* in, HttpRequest* r, const Route* rt, Value* out) {
  (void)in;
  Value req = v_obj();
  obj_set(&req, "method", v_str(r->method));
  obj_set(&req, "path", v_str(r->path));
  obj_set(&req, "query_raw", v_str(r->query ? r->query : ""));

  Value query = v_obj();
  if (r->query) {
    char* q = ew_strdup(r->query);
    char* save = q;
    while (*q) {
      char* amp = strchr(q, '&');
      if (amp) *amp = 0;
      char* eq = strchr(q, '=');
      char* k;
      char* v;
      if (eq) {
        *eq = 0;
        k = q;
        v = eq + 1;
      } else {
        k = q;
        v = (char*)"";
      }
      if (*k) obj_set(&query, k, v_str(v));
      if (!amp) break;
      q = amp + 1;
    }
    free(save);
  }
  obj_set(&req, "query", query);

  Value params = v_obj();
  if (rt) {
    for (int i = 0; i < rt->nparams; i++) {
      for (int j = 0; j < r->nparams; j++) {
        if (strcmp(r->param_names[j], rt->params[i]) == 0) {
          obj_set(&params, rt->params[i], v_str(r->param_values[j]));
          break;
        }
      }
    }
  }
  obj_set(&req, "params", params);

  Value headers = v_obj();
  for (int i = 0; i < r->nheaders; i++) {
    obj_set(&headers, r->header_names[i], v_str(r->header_values[i]));
  }
  obj_set(&req, "headers", headers);

  if (r->body && r->body_len) {
    char err[128];
    Value parsed = json_parse(r->body, r->body_len, err, sizeof(err));
    if (parsed.k != V_NULL) {
      obj_set(&req, "body", parsed);
    } else {
      obj_set(&req, "body", v_str(r->body));
    }
    obj_set(&req, "raw_body", v_str(r->body));
  } else {
    obj_set(&req, "body", v_null());
    obj_set(&req, "raw_body", v_str(""));
  }
  *out = req;
}

static bool route_matches(Route* rt, HttpRequest* r, const char* method) {
  if (ew_strncasecmp(rt->method, method, 16) != 0) return false;
  const char* rp = rt->path;
  const char* up = r->path;
  int pi = 0, ui = 0;
  while (rp[pi]) {
    if (rp[pi] == ':') {
      pi++;
      while (rp[pi] && rp[pi] != '/') pi++;
      while (up[ui] && up[ui] != '/') ui++;
      if (!up[ui] && rp[pi]) return false;
      continue;
    }
    if (rp[pi] != up[ui]) return false;
    pi++;
    ui++;
  }
  return up[ui] == 0;
}

static void send_error_page(HttpResponse* resp, Interp* in, int code, const char* title, const char* detail) {
  Buf b;
  buf_init(&b);
  buf_puts(&b, "<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
  buf_printf(&b, "<title>%d %s</title>", code, title);
  buf_puts(&b, "<style>body{font-family:system-ui,sans-serif;background:#0f1117;color:#e8eaf0;padding:60px;text-align:center}"
              "h1{font-size:64px;margin:0;color:#ff6b6b}p{color:#9aa4b2}</style></head><body>");
  buf_printf(&b, "<h1>%d</h1><p>%s</p>", code, title);
  if (detail) buf_printf(&b, "<p><code>%s</code></p>", detail);
  if (in && in->kid) {
    buf_puts(&b, "<p style=\"font-size:20px;color:#fdd835\">Oops! This page is hiding. Try another one!</p>");
  }
  buf_printf(&b, "<p style=\"font-size:12px\">easyweb %s</p>", EW_VERSION);
  buf_puts(&b, "</body></html>");
  http_response_send(resp, code, "text/html; charset=utf-8", b.data, b.len);
  buf_free(&b);
}

static void handle_request(ServerOpts* opts, HttpRequest* r, HttpResponse* resp) {
  Interp* in = opts->interp;
  const char* method = r->method;

  /* built in kid click logger */
  if (strcmp(r->path, "/__kid/click") == 0) {
    char* body = ew_strdup("{\"clicks\":0}");
    if (r->body && r->body_len) {
      char err[128];
      Value parsed = json_parse(r->body, r->body_len, err, sizeof(err));
      if (parsed.k == V_OBJ) {
        char* j = json_from_value(parsed);
        body = j;
      }
    }
    http_response_send(resp, 200, "application/json; charset=utf-8", body, strlen(body));
    free(body);
    return;
  }

  /* syntax highlighted source of the app */
  if (strcmp(r->path, "/__source") == 0) {
    char* src = NULL;
    size_t len = 0;
    if (in->app_path && path_read_file(in->app_path, &src, &len)) {
      char* hl = highlight_html(src, len, true);
      http_response_send(resp, 200, "text/html; charset=utf-8", hl, strlen(hl));
      free(hl);
      free(src);
      return;
    }
  }

  /* find a route */
  Route* found = NULL;
  for (int i = 0; i < in->routes.count; i++) {
    if (route_matches(&in->routes.routes[i], r, method)) {
      found = &in->routes.routes[i];
      break;
    }
  }

  if (found) {
    lock_get();
    in->resp_status = 200;
    in->resp_body_set = false;
    free(in->resp_content_type);
    in->resp_content_type = NULL;
    buf_free(&in->resp_headers);
    buf_init(&in->resp_headers);
    free(in->resp_location);
    in->resp_location = NULL;

    Env* env = env_new(in->globals);
    Value reqobj;
    build_req_object(in, r, found, &reqobj);
    env_define(env, "req", reqobj);
    value_release(&reqobj);

    Value result = v_null();
    bool ok = interp_call_route(in, found, env, &result);
    if (!ok) {
      Diag d = in->diag;
      if (opts->kidmode) {
        Buf b;
        buf_init(&b);
        buf_puts(&b, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Oops</title>"
                    "<style>body{font-family:system-ui,sans-serif;background:#111827;color:#f9fafb;padding:60px;text-align:center}"
                    "h1{font-size:42px;color:#fdd835}p{font-size:20px}</style></head><body>");
        buf_puts(&b, "<h1>Oops! Something went wrong</h1>");
        buf_printf(&b, "<p>%s</p>", d.message);
        if (d.hint[0]) buf_printf(&b, "<p style=\"color:#9ca3af\">%s</p>", d.hint);
        buf_printf(&b, "<p style=\"font-size:12px;color:#6b7280\">easyweb &middot; line %d</p>", d.line);
        buf_puts(&b, "</body></html>");
        http_response_send(resp, 500, "text/html; charset=utf-8", b.data, b.len);
        buf_free(&b);
      } else {
        fprintf(stderr, "\n[easyweb] error in route %s %s\n", r->method, r->path);
        diag_print(&d, true);
        send_error_page(resp, in, 500, "Internal Server Error", d.message);
      }
      in->failed = false;
      env_free(env);
      lock_release();
      return;
    }

    if (in->resp_location) {
      http_response_set_header(resp, "Location", in->resp_location);
      http_response_send(resp, in->resp_status, "text/plain; charset=utf-8", "", 0);
    } else if (in->resp_body_set) {
      http_response_send(resp, in->resp_status, in->resp_content_type ? in->resp_content_type : "application/json; charset=utf-8",
                         in->resp_body.data ? in->resp_body.data : "", in->resp_body.len);
    } else if (result.k == V_OBJ || result.k == V_ARR) {
      char* j = json_from_value(result);
      http_response_send(resp, in->resp_status, "application/json; charset=utf-8", j, strlen(j));
      free(j);
    } else if (result.k == V_STR) {
      http_response_send(resp, in->resp_status, in->resp_content_type ? in->resp_content_type : "text/html; charset=utf-8",
                         result.as.s->data, result.as.s->len);
    } else if (result.k == V_NULL) {
      http_response_send(resp, in->resp_status, "text/plain; charset=utf-8", "", 0);
    } else {
      char* s = value_to_string(result);
      http_response_send(resp, in->resp_status, "text/plain; charset=utf-8", s, strlen(s));
      free(s);
    }
    /* extra headers set from easyweb code */
    if (in->resp_headers.len) {
      char* hs = in->resp_headers.data;
      char* line = hs;
      while (line && *line) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char* colon = strchr(line, ':');
        if (colon) {
          *colon = 0;
          char* name = line;
          char* val = colon + 1;
          while (*val == ' ') val++;
          http_response_set_header(resp, name, val);
        }
        if (!nl) break;
        line = nl + 1;
      }
    }
    value_release(&result);
    env_free(env);
    lock_release();
    return;
  }

  /* main page */
  if (strcmp(method, "GET") == 0 && strcmp(r->path, "/") == 0) {
    lock_get();
    Env* env = env_new(in->globals);
    bool ok = true;
    char* html = render_page(in, env);
    env_free(env);
    lock_release();
    (void)ok;
    http_response_send(resp, 200, "text/html; charset=utf-8", html, strlen(html));
    free(html);
    return;
  }

  /* static files next to the app */
  if (strcmp(method, "GET") == 0 && !strstr(r->path, "..")) {
    char* dir = path_dirname(in->app_path ? in->app_path : ".");
    char* file = path_join(dir, r->path + 1);
    free(dir);
    if (path_exists(file) && !path_is_dir(file)) {
      char* data = NULL;
      size_t len = 0;
      if (path_read_file(file, &data, &len)) {
        http_response_send(resp, 200, mime_for(file), data, len);
        free(data);
        free(file);
        return;
      }
    }
    free(file);
  }

  send_error_page(resp, in, 404, "Not Found", r->path);
}

/* ------------------------------------------------------------------ */
/* connection handling                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
  ServerOpts* opts;
  char* raw;
  size_t len;
  ew_socket sock;
} ConnReal;

#ifdef _WIN32
static DWORD WINAPI conn_thread_real(LPVOID arg) {
#else
static void* conn_thread_real(void* arg) {
#endif
  ConnReal* c = (ConnReal*)arg;
  ServerOpts* opts = c->opts;
  ew_socket sock = c->sock;
  char* raw = c->raw;
  size_t len = c->len;
  free(c);

  HttpRequest req;
  HttpResponse resp;
  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));
  resp.status = 200;

  char err[128];
  if (!http_parse_request(raw, len, &req, err, sizeof(err))) {
    http_response_send(&resp, 400, "text/plain; charset=utf-8", "Bad Request", 11);
  } else {
    handle_request(opts, &req, &resp);
  }

  size_t out_len = 0;
  char* out = http_response_render(&resp, &out_len);
  size_t sent = 0;
  while (sent < out_len) {
    int n = (int)send(sock, out + sent, (int)(out_len - sent), 0);
    if (n <= 0) break;
    sent += (size_t)n;
  }
  free(out);
  http_response_free(&resp);
  http_request_free(&req);
  free(raw);

  shutdown(sock, EW_SHUT_BOTH);
  ew_close_socket(sock);
  g_active--;
  (void)opts;
  return 0;
}

/* ------------------------------------------------------------------ */
/* main loop                                                           */
/* ------------------------------------------------------------------ */

int server_run(ServerOpts* opts) {
  lock_init();

#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    fprintf(stderr, "easyweb: could not start the network\n");
    return 1;
  }
#endif

  ew_socket srv = socket(AF_INET, SOCK_STREAM, 0);
  if (srv == EW_INVALID) {
    fprintf(stderr, "easyweb: could not create a socket\n");
    return 1;
  }
  int yes = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)opts->port);
  if (!opts->host || !*opts->host || strcmp(opts->host, "0.0.0.0") == 0) {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    addr.sin_addr.s_addr = inet_addr(opts->host);
    if (addr.sin_addr.s_addr == INADDR_ANY) addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }

  if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "easyweb: port %d is already busy. Try: easyweb run --port %d file.eweb\n", opts->port,
            opts->port + 1);
    ew_close_socket(srv);
    return 1;
  }
  if (listen(srv, 64) != 0) {
    fprintf(stderr, "easyweb: could not listen on port %d\n", opts->port);
    ew_close_socket(srv);
    return 1;
  }

  char url[256];
  snprintf(url, sizeof(url), "http://localhost:%d", opts->port);
  printf("\n  easyweb %s is running\n", EW_VERSION);
  printf("  ------------------------------------------\n");
  printf("  app      %s\n", opts->app_path ? opts->app_path : "app.eweb");
  printf("  address  %s\n", url);
  printf("  mode     %s\n", opts->kidmode ? "kid mode (friendly errors)" : "normal");
  printf("  ------------------------------------------\n");
  printf("  press Ctrl+C to stop\n\n");
  fflush(stdout);

  if (opts->open_browser) {
    open_browser(url);
  }

  for (;;) {
    struct sockaddr_in cli;
#ifdef _WIN32
    int clen = sizeof(cli);
#else
    socklen_t clen = sizeof(cli);
#endif
    ew_socket c = accept(srv, (struct sockaddr*)&cli, &clen);
    if (c == EW_INVALID) {
      if (errno == EINTR) continue;
      break;
    }
    if (g_active >= g_max_threads) {
      const char* busy = "HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      send(c, busy, (int)strlen(busy), 0);
      ew_close_socket(c);
      continue;
    }
    /* read the request */
    size_t cap = 8192, len = 0;
    char* raw = (char*)ew_malloc(cap);
    for (;;) {
      if (len == cap) {
        cap *= 2;
        if (cap > 4 * 1024 * 1024) break;
        raw = (char*)ew_realloc(raw, cap);
      }
      int n = (int)recv(c, raw + len, (int)(cap - len), 0);
      if (n <= 0) break;
      len += (size_t)n;
      /* stop as soon as we have the full body */
      const char* he = strstr(raw, "\r\n\r\n");
      if (he) {
        size_t hlen = (size_t)(he - raw) + 4;
        size_t cl = 0;
        for (size_t i = 0; i + 15 < hlen; i++) {
          if (ew_strncasecmp(raw + i, "Content-Length:", 15) == 0) {
            cl = (size_t)atol(raw + i + 15);
            break;
          }
        }
        if (len >= hlen + cl) break;
      }
    }
    if (len == 0) {
      free(raw);
      ew_close_socket(c);
      continue;
    }
    g_active++;
    ConnReal* cr = (ConnReal*)ew_calloc(1, sizeof(ConnReal));
    cr->opts = opts;
    cr->raw = raw;
    cr->len = len;
    cr->sock = c;
    EW_THREAD_CALL(conn_thread_real, cr);
  }

  ew_close_socket(srv);
#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
