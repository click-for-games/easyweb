/* easyweb - tree walking interpreter (server side). */
#include "ew.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum { EX_NORMAL = 0, EX_BREAK, EX_CONTINUE, EX_RETURN } ExecResult;

static bool eval_node(Interp* in, Env* env, Node* n, Value* out);
static ExecResult exec_stmt(Interp* in, Env* env, Node* n);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void set_str_out(Value* out, const char* s) {
  *out = v_str(s);
}


Interp* interp_new(bool kidmode) {
  Interp* in = (Interp*)ew_calloc(1, sizeof(Interp));
  in->globals = env_new(NULL);
  buf_init(&in->css);
  buf_init(&in->client_js);
  buf_init(&in->pkg_func_js);
  buf_init(&in->resp_headers);
  buf_init(&in->resp_body);
  in->page_title = ew_strdup("easyweb app");
  in->port = EW_DEFAULT_PORT;
  in->kid = kidmode;
  in->kidmode = ew_strdup(kidmode ? "on" : "off");
  in->retval = v_null();
  diag_init(&in->diag);
  interp_register_builtins(in);
  return in;
}

void interp_free(Interp* in) {
  if (!in) return;
  env_free(in->globals);
  for (int i = 0; i < in->routes.count; i++) {
    free(in->routes.routes[i].method);
    free(in->routes.routes[i].path);
    for (int j = 0; j < in->routes.routes[i].nparams; j++) {
      free(in->routes.routes[i].params[j]);
    }
    free(in->routes.routes[i].params);
  }
  free(in->routes.routes);
  buf_free(&in->css);
  buf_free(&in->client_js);
  buf_free(&in->pkg_func_js);
  buf_free(&in->resp_headers);
  buf_free(&in->resp_body);
  free(in->page_title);
  free(in->kidmode);
  free(in->ui_raw);
  free(in->kid_raw);
  free(in->resp_content_type);
  free(in->resp_location);
  free(in->app_path);
  free(in->app_dir);
  free(in->app_name);
  node_free(in->root);
  for (int i = 0; i < in->npkg_progs; i++) node_free(in->pkg_progs[i]);
  free(in->pkg_progs);
  free(in->script_blocks);
  free(in->client_funcs);
  value_release(&in->retval);
  free(in);
}

static void interp_error(Interp* in, int line, int col, const char* code, const char* fmt, ...) {
  if (in->failed) return;
  in->failed = true;
  char msg[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  diag_set(&in->diag, in->app_path, line, col, code, "%s", msg);
}

/* ------------------------------------------------------------------ */
/* route table                                                         */
/* ------------------------------------------------------------------ */

static void route_add(Interp* in, const char* method, const char* path, Node* handler) {
  RouteTable* t = &in->routes;
  if (t->count == t->cap) {
    t->cap = t->cap ? t->cap * 2 : 8;
    t->routes = (Route*)ew_realloc(t->routes, sizeof(Route) * (size_t)t->cap);
  }
  Route* r = &t->routes[t->count++];
  memset(r, 0, sizeof(Route));
  r->method = ew_strdup(method);
  r->path = ew_strdup(path);
  r->handler = handler;
  /* collect :params */
  const char* p = path;
  while ((p = strchr(p, ':')) != NULL) {
    const char* e = p + 1;
    while (*e && *e != '/' && *e != ' ') e++;
    if (e > p + 1) {
      r->params = (char**)ew_realloc(r->params, sizeof(char*) * (size_t)(r->nparams + 1));
      r->params[r->nparams++] = ew_strndup(p + 1, (size_t)(e - p - 1));
    }
    p = e;
  }
}

/* ------------------------------------------------------------------ */
/* builtin functions                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
  const char* name;
  NativeFn fn;
} Builtin;

static void bi_print(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  Buf b;
  buf_init(&b);
  for (int i = 0; i < nargs; i++) {
    if (i) buf_puts(&b, " ");
    char* s = value_to_string(args[i]);
    buf_puts(&b, s);
    free(s);
  }
  printf("[easyweb] %s\n", b.data ? b.data : "");
  fflush(stdout);
  buf_free(&b);
  *out = v_null();
}

static void bi_len(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double n = 0;
  if (nargs > 0) {
    if (args[0].k == V_STR) n = (double)args[0].as.s->len;
    else if (args[0].k == V_ARR) n = (double)arr_len(&args[0]);
    else if (args[0].k == V_OBJ) n = (double)obj_count(&args[0]);
  }
  *out = v_num(n);
}

static void bi_type(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  const char* t = "null";
  if (nargs > 0) {
    switch (args[0].k) {
      case V_NULL: t = "null"; break;
      case V_BOOL: t = "bool"; break;
      case V_NUM: t = "number"; break;
      case V_STR: t = "string"; break;
      case V_ARR: t = "array"; break;
      case V_OBJ: t = "object"; break;
      case V_FUN: t = "function"; break;
    }
  }
  set_str_out(out, t);
}

static void bi_str(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0) { *out = v_str(""); return; }
  char* s = value_to_string(args[0]);
  *out = v_str(s);
  free(s);
}

static void bi_num(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double d = 0;
  if (nargs > 0) {
    if (args[0].k == V_NUM) d = args[0].as.n;
    else if (args[0].k == V_BOOL) d = args[0].as.b ? 1 : 0;
    else if (args[0].k == V_STR) d = atof(args[0].as.s->data);
  }
  *out = v_num(d);
}

static void bi_int(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double d = 0;
  if (nargs > 0) {
    if (args[0].k == V_NUM) d = args[0].as.n;
    else if (args[0].k == V_BOOL) d = args[0].as.b ? 1 : 0;
    else if (args[0].k == V_STR) d = atof(args[0].as.s->data);
  }
  *out = v_num((double)(long long)d);
}

static void bi_bool(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  *out = v_bool(nargs > 0 ? value_truthy(args[0]) : false);
}

static void bi_json(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0) { *out = v_str("null"); return; }
  char* s = json_from_value(args[0]);
  *out = v_str(s);
  free(s);
}

static void bi_parse_json(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0) { *out = v_null(); return; }
  if (args[0].k != V_STR) {
    /* already an object or array - just hand it back */
    *out = args[0];
    value_retain(out);
    return;
  }
  char err[128];
  err[0] = 0;
  Value v = json_parse(args[0].as.s->data, args[0].as.s->len, err, sizeof(err));
  if (v.k == V_NULL && err[0]) { *out = v_null(); return; }
  *out = v;
}

static void bi_now(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  (void)in; (void)args; (void)nargs;
  *out = v_num((double)time(NULL));
}

static void bi_range(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double a = 0, b = 0;
  if (nargs == 1) b = args[0].k == V_NUM ? args[0].as.n : 0;
  else if (nargs >= 2) {
    a = args[0].k == V_NUM ? args[0].as.n : 0;
    b = args[1].k == V_NUM ? args[1].as.n : 0;
  }
  Value arr = arr_new();
  for (double i = a; i < b; i++) arr_push(&arr, v_num(i));
  *out = arr;
}

static void bi_keys(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  Value arr = arr_new();
  if (nargs > 0 && args[0].k == V_OBJ) {
    char** keys = obj_keys(&args[0]);
    for (int i = 0; keys[i]; i++) {
      arr_push(&arr, v_str(keys[i]));
      free(keys[i]);
    }
    free(keys);
  }
  *out = arr;
}

static void bi_values(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  Value arr = arr_new();
  if (nargs > 0 && args[0].k == V_OBJ) {
    char** keys = obj_keys(&args[0]);
    for (int i = 0; keys[i]; i++) {
      Value v;
      if (obj_get(&args[0], keys[i], &v)) {
        arr_push(&arr, v);
        value_release(&v);
      }
      free(keys[i]);
    }
    free(keys);
  }
  *out = arr;
}

static void bi_push(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs >= 2 && args[0].k == V_ARR) {
    arr_push(&args[0], args[1]);
    *out = v_num((double)arr_len(&args[0]));
    return;
  }
  *out = v_null();
}

static void bi_pop(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs >= 1 && args[0].k == V_ARR && arr_len(&args[0]) > 0) {
    Value v = arr_get(&args[0], arr_len(&args[0]) - 1);
    value_retain(&v);
    arr_remove_at(&args[0], arr_len(&args[0]) - 1);
    *out = v;
    return;
  }
  *out = v_null();
}

static void bi_upper(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_STR) { *out = v_str(""); return; }
  char* s = ew_strdup(args[0].as.s->data);
  for (char* q = s; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
  *out = v_str(s);
  free(s);
}

static void bi_lower(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_STR) { *out = v_str(""); return; }
  char* s = ew_strdup(args[0].as.s->data);
  for (char* q = s; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32;
  *out = v_str(s);
  free(s);
}

static void bi_trim(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_STR) { *out = v_str(""); return; }
  const char* s = args[0].as.s->data;
  while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
  const char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
  *out = v_strn(s, (size_t)(e - s));
}

static void bi_split(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  Value arr = arr_new();
  if (nargs == 0 || args[0].k != V_STR) { *out = arr; return; }
  const char* sep = (nargs > 1 && args[1].k == V_STR) ? args[1].as.s->data : " ";
  size_t sl = strlen(sep);
  const char* s = args[0].as.s->data;
  if (sl == 0) {
    arr_push(&arr, v_str(s));
    *out = arr;
    return;
  }
  const char* p = s;
  for (;;) {
    const char* q = strstr(p, sep);
    if (!q) {
      arr_push(&arr, v_str(p));
      break;
    }
    arr_push(&arr, v_strn(p, (size_t)(q - p)));
    p = q + sl;
  }
  *out = arr;
}

static void bi_join(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_ARR) { *out = v_str(""); return; }
  const char* sep = (nargs > 1 && args[1].k == V_STR) ? args[1].as.s->data : ",";
  Buf b;
  buf_init(&b);
  for (size_t i = 0; i < arr_len(&args[0]); i++) {
    if (i) buf_puts(&b, sep);
    Value v = arr_get(&args[0], i);
    char* s = value_to_string(v);
    buf_puts(&b, s);
    free(s);
  }
  *out = v_str(b.data ? b.data : "");
  buf_free(&b);
}

static double num_arg(Value* args, int nargs, int i) {
  if (i >= nargs) return 0;
  if (args[i].k == V_NUM) return args[i].as.n;
  if (args[i].k == V_STR) return atof(args[i].as.s->data);
  if (args[i].k == V_BOOL) return args[i].as.b ? 1 : 0;
  return 0;
}

static void bi_abs(Interp* in, Value* args, int nargs, Value* out) { (void)in; *out = v_num(fabs(num_arg(args, nargs, 0))); }
static void bi_round(Interp* in, Value* args, int nargs, Value* out) { (void)in; *out = v_num(round(num_arg(args, nargs, 0))); }
static void bi_floor(Interp* in, Value* args, int nargs, Value* out) { (void)in; *out = v_num(floor(num_arg(args, nargs, 0))); }
static void bi_ceil(Interp* in, Value* args, int nargs, Value* out) { (void)in; *out = v_num(ceil(num_arg(args, nargs, 0))); }
static void bi_min(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double m = num_arg(args, nargs, 0);
  for (int i = 1; i < nargs; i++) {
    double v = num_arg(args, nargs, i);
    if (v < m) m = v;
  }
  *out = v_num(m);
}
static void bi_max(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double m = num_arg(args, nargs, 0);
  for (int i = 1; i < nargs; i++) {
    double v = num_arg(args, nargs, i);
    if (v > m) m = v;
  }
  *out = v_num(m);
}

static void bi_escape(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_STR) { *out = v_str(""); return; }
  Buf b;
  buf_init(&b);
  const char* s = args[0].as.s->data;
  for (const char* p = s; *p; p++) {
    switch (*p) {
      case '&': buf_puts(&b, "&amp;"); break;
      case '<': buf_puts(&b, "&lt;"); break;
      case '>': buf_puts(&b, "&gt;"); break;
      case '"': buf_puts(&b, "&quot;"); break;
      case '\'': buf_puts(&b, "&#39;"); break;
      default: buf_putc(&b, *p);
    }
  }
  *out = v_str(b.data ? b.data : "");
  buf_free(&b);
}

static void bi_status(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs > 0) in->resp_status = (int)num_arg(args, nargs, 0);
  *out = v_null();
}

static void bi_header(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs >= 2) {
    char* name = value_to_string(args[0]);
    char* value = value_to_string(args[1]);
    buf_printf(&in->resp_headers, "%s: %s\n", name, value);
    free(name);
    free(value);
  }
  *out = v_null();
}

static void bi_redirect(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs > 0) {
    char* url = value_to_string(args[0]);
    free(in->resp_location);
    in->resp_location = url;
    in->resp_status = 302;
  }
  *out = v_null();
}

static void bi_html(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  (void)args;
  (void)nargs;
  free(in->resp_content_type);
  in->resp_content_type = ew_strdup("text/html; charset=utf-8");
  *out = v_null();
}

static void bi_send(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs > 0) {
    char* s = json_from_value(args[0]);
    free(in->resp_content_type);
    in->resp_content_type = ew_strdup("application/json; charset=utf-8");
    buf_free(&in->resp_body);
    buf_init(&in->resp_body);
    buf_puts(&in->resp_body, s);
    in->resp_body_set = true;
    free(s);
  }
  *out = v_null();
}

/* file access is locked to the app folder (security).
   Relative names are looked up next to the app file. */
static char* safe_resolve(Interp* in, const char* p) {
  if (!p || !*p) return NULL;
  if (strstr(p, "..")) return NULL;
  bool absolute = (p[0] == '/' || p[0] == '\\' || (p[1] == ':' && p[2] != 0));
  char* full;
  if (absolute) {
    full = ew_strdup(p);
  } else {
    full = path_join(in->app_dir ? in->app_dir : ".", p);
  }
  if (in->app_dir && *in->app_dir) {
    size_t n = strlen(in->app_dir);
    if (strncmp(full, in->app_dir, n) != 0) {
      free(full);
      return NULL;
    }
    if (full[n] != 0 && full[n] != '/' && full[n] != '\\') {
      free(full);
      return NULL;
    }
  }
  return full;
}

static void bi_read_file(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_STR) { *out = v_null(); return; }
  char* path = safe_resolve(in, args[0].as.s->data);
  if (!path) { *out = v_null(); return; }
  char* data = NULL;
  size_t len = 0;
  if (!path_read_file(path, &data, &len)) { *out = v_null(); free(path); return; }
  *out = v_strn(data, len);
  free(data);
  free(path);
}

static void bi_write_file(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs < 2 || args[0].k != V_STR) { *out = v_bool(false); return; }
  char* path = safe_resolve(in, args[0].as.s->data);
  if (!path) { *out = v_bool(false); return; }
  char* text = value_to_string(args[1]);
  bool ok = path_write_file(path, text, strlen(text));
  free(text);
  free(path);
  *out = v_bool(ok);
}

static void bi_file_exists(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  if (nargs == 0 || args[0].k != V_STR) { *out = v_bool(false); return; }
  char* path = safe_resolve(in, args[0].as.s->data);
  if (!path) { *out = v_bool(false); return; }
  *out = v_bool(path_exists(path));
  free(path);
}

static void bi_app_name(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  (void)args; (void)nargs;
  set_str_out(out, in->app_name ? in->app_name : "app");
}

static void bi_app_dir(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  (void)args; (void)nargs;
  set_str_out(out, in->app_dir ? in->app_dir : ".");
}

static void bi_random(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  double n = num_arg(args, nargs, 0);
  if (n <= 0) n = 1;
  *out = v_num((double)(rand() % (int)n));
}

static void bi_input(Interp* in, Value* args, int nargs, Value* out) {
  (void)in;
  (void)in; (void)args; (void)nargs;
  set_str_out(out, "");
}

static const Builtin BUILTINS[] = {
    {"print", bi_print},       {"log", bi_print},       {"len", bi_len},
    {"type", bi_type},         {"str", bi_str},         {"num", bi_num},
    {"int", bi_int},           {"bool", bi_bool},       {"json", bi_json},
    {"parse_json", bi_parse_json}, {"now", bi_now},     {"range", bi_range},
    {"keys", bi_keys},         {"values", bi_values},   {"push", bi_push},
    {"pop", bi_pop},           {"upper", bi_upper},     {"lower", bi_lower},
    {"trim", bi_trim},         {"split", bi_split},     {"join", bi_join},
    {"abs", bi_abs},           {"round", bi_round},     {"floor", bi_floor},
    {"ceil", bi_ceil},         {"min", bi_min},         {"max", bi_max},
    {"escape", bi_escape},     {"status", bi_status},   {"header", bi_header},
    {"redirect", bi_redirect}, {"html", bi_html},       {"send", bi_send},
    {"read_file", bi_read_file}, {"write_file", bi_write_file},
    {"file_exists", bi_file_exists}, {"app_name", bi_app_name},
    {"app_dir", bi_app_dir},   {"random", bi_random},   {"input", bi_input},
    {NULL, NULL}};

void interp_register_builtins(Interp* in) {
  for (int i = 0; BUILTINS[i].name; i++) {
    Value v = v_native(BUILTINS[i].name, BUILTINS[i].fn);
    env_define(in->globals, BUILTINS[i].name, v);
    value_release(&v);
  }
}

/* ------------------------------------------------------------------ */
/* calling                                                             */
/* ------------------------------------------------------------------ */

static bool call_function(Interp* in, Value fn, Value* args, int nargs, Value* out) {
  if (fn.k != V_FUN) {
    interp_error(in, 0, 0, "E200", "This thing is not a function");
    return false;
  }
  Func* f = fn.as.f;
  if (f->native) {
    f->native(in, args, nargs, out);
    return true;
  }
  if (!f->decl || !f->decl->body) {
    *out = v_null();
    return true;
  }
  if (++in->depth > 512) {
    in->depth--;
    interp_error(in, f->decl->line, f->decl->col, "E201", "Too much recursion - function calls never ended");
    return false;
  }
  Env* env = env_new(f->env);
  for (int i = 0; i < f->decl->count; i++) {
    Node* p = f->decl->items[i];
    Value av = (i < nargs) ? args[i] : v_null();
    value_retain(&av);
    env_define(env, p->str, av);
    value_release(&av);
  }
  in->has_ret = false;
  in->retval = v_null();
  ExecResult r = EX_NORMAL;
  for (int i = 0; i < f->decl->body->count; i++) {
    r = exec_stmt(in, env, f->decl->body->items[i]);
    if (r != EX_NORMAL) break;
  }
  if (in->has_ret) {
    *out = in->retval;
    in->retval = v_null();
  } else {
    *out = v_null();
  }
  in->has_ret = false;
  env_free(env);
  in->depth--;
  return true;
}

/* ------------------------------------------------------------------ */
/* member access                                                       */
/* ------------------------------------------------------------------ */

static bool eval_member(Interp* in, Env* env, Node* n, Value* out) {
  Value base;
  if (!eval_node(in, env, n->left, &base)) return false;
  if (base.k == V_ARR && strcmp(n->str, "length") == 0) {
    *out = v_num((double)arr_len(&base));
    value_release(&base);
    return true;
  }
  if (base.k == V_STR && strcmp(n->str, "length") == 0) {
    *out = v_num((double)base.as.s->len);
    value_release(&base);
    return true;
  }
  if (base.k == V_OBJ) {
    Value v;
    if (obj_get(&base, n->str, &v)) {
      *out = v;
      value_release(&base);
      return true;
    }
  }
  value_release(&base);
  interp_error(in, n->line, n->col, "E202", "No property called '%s'", n->str);
  return false;
}

/* ------------------------------------------------------------------ */
/* evaluation                                                          */
/* ------------------------------------------------------------------ */

static bool eval_call(Interp* in, Env* env, Node* n, Value* out) {
  Value args[16];
  int nargs = 0;
  Value* dynargs = NULL;
  int dyncap = 0;

  for (int i = 0; i < n->count; i++) {
    Value a;
    if (!eval_node(in, env, n->items[i], &a)) {
      for (int j = 0; j < nargs; j++) {
        if (dynargs && j >= 16) value_release(&dynargs[j]);
        else value_release(&args[j]);
      }
      free(dynargs);
      return false;
    }
    if (nargs < 16) {
      args[nargs] = a;
    } else {
      if (nargs == dyncap) {
        dyncap = dyncap ? dyncap * 2 : 32;
        dynargs = (Value*)ew_realloc(dynargs, sizeof(Value) * (size_t)dyncap);
      }
      dynargs[nargs] = a;
    }
    nargs++;
  }

  Value fn;
  Node* callee = n->left;

  if (callee->kind == N_MEMBER) {
    Value base;
    if (!eval_node(in, env, callee->left, &base)) {
      for (int j = 0; j < nargs; j++) {
        if (dynargs && j >= 16) value_release(&dynargs[j]);
        else value_release(&args[j]);
      }
      free(dynargs);
      return false;
    }
    /* array methods */
    if (base.k == V_ARR) {
      const char* m = callee->str;
      if (strcmp(m, "push") == 0 && nargs >= 1) {
        arr_push(&base, args[0]);
        *out = v_num((double)arr_len(&base));
        value_release(&base);
        goto cleanup;
      }
      if (strcmp(m, "pop") == 0) {
        Value v = v_null();
        if (arr_len(&base) > 0) {
          v = arr_get(&base, arr_len(&base) - 1);
          value_retain(&v);
          arr_remove_at(&base, arr_len(&base) - 1);
        }
        *out = v;
        value_release(&base);
        goto cleanup;
      }
      if (strcmp(m, "join") == 0) {
        const char* sep = (nargs >= 1 && args[0].k == V_STR) ? args[0].as.s->data : ",";
        Buf b;
        buf_init(&b);
        for (size_t i = 0; i < arr_len(&base); i++) {
          if (i) buf_puts(&b, sep);
          Value item = arr_get(&base, i);
          char* s = value_to_string(item);
          buf_puts(&b, s);
          free(s);
        }
        *out = v_str(b.data ? b.data : "");
        buf_free(&b);
        value_release(&base);
        goto cleanup;
      }
      if (strcmp(m, "reverse") == 0) {
        size_t n = arr_len(&base);
        for (size_t i = 0; i < n / 2; i++) {
          Value tmp = base.as.a->items[i];
          base.as.a->items[i] = base.as.a->items[n - 1 - i];
          base.as.a->items[n - 1 - i] = tmp;
        }
        *out = base;
        goto cleanup;
      }
      if (strcmp(m, "contains") == 0 && nargs >= 1) {
        bool found = false;
        for (size_t i = 0; i < arr_len(&base); i++) {
          Value item = arr_get(&base, i);
          if (value_equals(item, args[0])) { found = true; break; }
        }
        *out = v_bool(found);
        value_release(&base);
        goto cleanup;
      }
    }
    /* string methods */
    if (base.k == V_STR) {
      const char* m = callee->str;
      const char* s = base.as.s->data;
      if (strcmp(m, "upper") == 0) {
        char* d = ew_strdup(s);
        for (char* q = d; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
        *out = v_str(d);
        free(d);
        value_release(&base);
        goto cleanup;
      }
      if (strcmp(m, "lower") == 0) {
        char* d = ew_strdup(s);
        for (char* q = d; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32;
        *out = v_str(d);
        free(d);
        value_release(&base);
        goto cleanup;
      }
      if (strcmp(m, "contains") == 0 && nargs >= 1) {
        char* needle = value_to_string(args[0]);
        bool found = strstr(s, needle) != NULL;
        free(needle);
        *out = v_bool(found);
        value_release(&base);
        goto cleanup;
      }
      if (strcmp(m, "split") == 0) {
        char* sep = (nargs >= 1 && args[0].k == V_STR) ? args[0].as.s->data : " ";
        Value arr = arr_new();
        size_t sl = strlen(sep);
        const char* p = s;
        if (sl == 0) {
          arr_push(&arr, v_str(p));
        } else {
          for (;;) {
            const char* q = strstr(p, sep);
            if (!q) { arr_push(&arr, v_str(p)); break; }
            arr_push(&arr, v_strn(p, (size_t)(q - p)));
            p = q + sl;
          }
        }
        *out = arr;
        value_release(&base);
        goto cleanup;
      }
    }
    if (base.k == V_OBJ) {
      Value v;
      if (obj_get(&base, callee->str, &v)) {
        fn = v;
        value_release(&base);
        goto have;
      }
    }
    value_release(&base);
    interp_error(in, n->line, n->col, "E203", "There is no '%s' here", callee->str);
    goto cleanup;
  }

  if (callee->kind == N_IDENT) {
    Value v;
    if (env_get(env, callee->str, &v)) {
      fn = v;
      goto have;
    }
    interp_error(in, n->line, n->col, "E204", "Unknown name '%s'", callee->str);
    goto cleanup;
  }

  interp_error(in, n->line, n->col, "E205", "This cannot be called");
  goto cleanup;

have:
  if (fn.k != V_FUN) {
    interp_error(in, n->line, n->col, "E206", "'%s' is not a function", callee->str ? callee->str : "value");
    value_release(&fn);
    goto cleanup;
  }
  {
    Value* argv = args;
    Value* tmp = NULL;
    if (dynargs) {
      tmp = (Value*)ew_calloc((size_t)nargs, sizeof(Value));
      for (int i = 0; i < nargs; i++) tmp[i] = (i < 16) ? args[i] : dynargs[i];
      argv = tmp;
    }
    bool ok = call_function(in, fn, argv, nargs, out);
    free(tmp);
    value_release(&fn);
    if (!ok) goto cleanup;
  }

cleanup:
  for (int j = 0; j < nargs; j++) {
    if (dynargs && j >= 16) value_release(&dynargs[j]);
    else value_release(&args[j]);
  }
  free(dynargs);
  return !in->failed;
}

/* combines the current value with the new one for += -= *= /= */
static Value compound_value(const char* op, Value cur, Value rhs, bool* ok) {
  *ok = true;
  if (strcmp(op, "+=") == 0) {
    if (cur.k == V_STR) {
      char* s = value_to_string(rhs);
      char* merged = ew_asprintf("%s%s", cur.as.s->data, s);
      Value r = v_str(merged);
      free(s);
      free(merged);
      return r;
    }
    return v_num(num_arg(&cur, 1, 0) + num_arg(&rhs, 1, 0));
  }
  if (strcmp(op, "-=") == 0) return v_num(num_arg(&cur, 1, 0) - num_arg(&rhs, 1, 0));
  if (strcmp(op, "*=") == 0) return v_num(num_arg(&cur, 1, 0) * num_arg(&rhs, 1, 0));
  if (strcmp(op, "/=") == 0) {
    double d = num_arg(&rhs, 1, 0);
    return v_num(d == 0 ? 0 : num_arg(&cur, 1, 0) / d);
  }
  *ok = false;
  return v_null();
}

static bool eval_assign(Interp* in, Env* env, Node* n, Value* out) {
  Value rhs;
  if (!eval_node(in, env, n->right, &rhs)) return false;

  Node* target = n->left;
  if (target->kind == N_IDENT) {
    if (strcmp(n->op, "=") == 0) {
      env_set(env, target->str, rhs);
      *out = rhs; /* our reference goes to the caller */
      return true;
    }
    Value cur;
    if (!env_get(env, target->str, &cur)) {
      interp_error(in, n->line, n->col, "E207", "Unknown name '%s'", target->str);
      value_release(&rhs);
      return false;
    }
    bool ok = true;
    Value res = compound_value(n->op, cur, rhs, &ok);
    value_release(&cur);
    if (!ok) {
      interp_error(in, n->line, n->col, "E207", "I do not know how to use '%s' here", n->op);
      value_release(&rhs);
      return false;
    }
    env_set(env, target->str, res);
    value_release(&rhs);
    *out = res; /* our reference goes to the caller */
    return true;
  }

  if (target->kind == N_MEMBER) {
    Value base;
    if (!eval_node(in, env, target->left, &base)) {
      value_release(&rhs);
      return false;
    }
    if (base.k != V_OBJ) {
      interp_error(in, n->line, n->col, "E208", "Cannot set a property on this value");
      value_release(&base);
      value_release(&rhs);
      return false;
    }
    Value res = rhs;
    if (strcmp(n->op, "=") != 0) {
      Value cur;
      if (!obj_get(&base, target->str, &cur)) {
        interp_error(in, n->line, n->col, "E208", "There is no property called '%s' yet", target->str);
        value_release(&base);
        value_release(&rhs);
        return false;
      }
      bool ok = true;
      res = compound_value(n->op, cur, rhs, &ok);
      value_release(&cur);
      if (!ok) {
        interp_error(in, n->line, n->col, "E208", "I do not know how to use '%s' here", n->op);
        value_release(&base);
        value_release(&rhs);
        return false;
      }
      value_release(&rhs);
    }
    obj_set(&base, target->str, res);
    *out = res; /* our reference goes to the caller */
    value_release(&base);
    return true;
  }

  if (target->kind == N_INDEX) {
    Value base;
    if (!eval_node(in, env, target->left, &base)) {
      value_release(&rhs);
      return false;
    }
    Value key;
    if (!eval_node(in, env, target->right, &key)) {
      value_release(&base);
      value_release(&rhs);
      return false;
    }
    Value res = rhs;
    if (strcmp(n->op, "=") != 0) {
      Value cur = value_index(base, key);
      bool ok = true;
      res = compound_value(n->op, cur, rhs, &ok);
      value_release(&cur);
      if (!ok) {
        interp_error(in, n->line, n->col, "E209", "I do not know how to use '%s' here", n->op);
        value_release(&key);
        value_release(&base);
        value_release(&rhs);
        return false;
      }
      value_release(&rhs);
    }
    value_set_index(base, key, res);
    value_release(&key);
    value_release(&base);
    *out = res; /* our reference goes to the caller */
    return true;
  }

  interp_error(in, n->line, n->col, "E209", "You cannot assign to this");
  value_release(&rhs);
  return false;
}

static bool eval_node(Interp* in, Env* env, Node* n, Value* out) {
  if (!n) {
    *out = v_null();
    return true;
  }
  if (in->failed) return false;

  switch (n->kind) {
    case N_NUM:
      *out = v_num(n->num);
      return true;
    case N_STR:
      *out = v_str(n->str ? n->str : "");
      return true;
    case N_BOOL:
      *out = v_bool(n->bval);
      return true;
    case N_NULL:
      *out = v_null();
      return true;
    case N_IDENT: {
      Value v;
      if (env_get(env, n->str, &v)) {
        *out = v;
        return true;
      }
      interp_error(in, n->line, n->col, "E210", "Unknown name '%s'", n->str);
      return false;
    }
    case N_BIN: {
      Value a, b;
      if (!eval_node(in, env, n->left, &a)) return false;
      if (!eval_node(in, env, n->right, &b)) {
        value_release(&a);
        return false;
      }
      const char* op = n->op;
      if (strcmp(op, "&&") == 0) {
        if (!value_truthy(a)) { *out = v_bool(false); value_release(&a); value_release(&b); return true; }
        value_release(&a);
        *out = v_bool(value_truthy(b));
        value_release(&b);
        return true;
      }
      if (strcmp(op, "||") == 0) {
        if (value_truthy(a)) { *out = v_bool(true); value_release(&a); value_release(&b); return true; }
        value_release(&a);
        *out = v_bool(value_truthy(b));
        value_release(&b);
        return true;
      }
      if (strcmp(op, "?:") == 0) {
        value_release(&b);
        if (value_truthy(a)) {
          *out = a;
          value_release(&a);
          return true;
        }
        value_release(&a);
        Value c;
        if (!eval_node(in, env, n->third, &c)) return false;
        *out = c;
        return true;
      }
      if (strcmp(op, "==") == 0) {
        *out = v_bool(value_equals(a, b));
      } else if (strcmp(op, "!=") == 0) {
        *out = v_bool(!value_equals(a, b));
      } else if (strcmp(op, "+") == 0) {
        if (a.k == V_STR || b.k == V_STR) {
          char* x = value_to_string(a);
          char* y = value_to_string(b);
          char* m = ew_asprintf("%s%s", x, y);
          *out = v_str(m);
          free(x); free(y); free(m);
        } else if (a.k == V_ARR && b.k == V_ARR) {
          Value arr = arr_new();
          for (size_t i = 0; i < arr_len(&a); i++) {
            Value it = arr_get(&a, i);
            arr_push(&arr, it);
          }
          for (size_t i = 0; i < arr_len(&b); i++) {
            Value it = arr_get(&b, i);
            arr_push(&arr, it);
          }
          *out = arr;
        } else {
          *out = v_num(num_arg(&a, 1, 0) + num_arg(&b, 1, 0));
        }
      } else if (strcmp(op, "-") == 0) {
        *out = v_num(num_arg(&a, 1, 0) - num_arg(&b, 1, 0));
      } else if (strcmp(op, "*") == 0) {
        *out = v_num(num_arg(&a, 1, 0) * num_arg(&b, 1, 0));
      } else if (strcmp(op, "/") == 0) {
        double d = num_arg(&b, 1, 0);
        *out = v_num(d == 0 ? 0 : num_arg(&a, 1, 0) / d);
      } else if (strcmp(op, "%") == 0) {
        double d = num_arg(&b, 1, 0);
        *out = v_num(d == 0 ? 0 : fmod(num_arg(&a, 1, 0), d));
      } else if (strcmp(op, "<") == 0) {
        *out = v_bool(value_compare(a, b) < 0);
      } else if (strcmp(op, "<=") == 0) {
        *out = v_bool(value_compare(a, b) <= 0);
      } else if (strcmp(op, ">") == 0) {
        *out = v_bool(value_compare(a, b) > 0);
      } else if (strcmp(op, ">=") == 0) {
        *out = v_bool(value_compare(a, b) >= 0);
      } else {
        value_release(&a);
        value_release(&b);
        interp_error(in, n->line, n->col, "E211", "Unknown operator '%s'", op);
        return false;
      }
      value_release(&a);
      value_release(&b);
      return true;
    }
    case N_UN: {
      Value a;
      if (!eval_node(in, env, n->left, &a)) return false;
      if (strcmp(n->op, "!") == 0) {
        *out = v_bool(!value_truthy(a));
      } else {
        *out = v_num(-num_arg(&a, 1, 0));
      }
      value_release(&a);
      return true;
    }
    case N_ASSIGN:
      return eval_assign(in, env, n, out);
    case N_CALL:
      return eval_call(in, env, n, out);
    case N_MEMBER:
      return eval_member(in, env, n, out);
    case N_INDEX: {
      Value base, key;
      if (!eval_node(in, env, n->left, &base)) return false;
      if (!eval_node(in, env, n->right, &key)) {
        value_release(&base);
        return false;
      }
      Value v = value_index(base, key);
      value_release(&base);
      value_release(&key);
      *out = v;
      return true;
    }
    case N_ARRAY: {
      Value arr = arr_new();
      for (int i = 0; i < n->count; i++) {
        Value item;
        if (!eval_node(in, env, n->items[i], &item)) {
          value_release(&arr);
          return false;
        }
        arr_push(&arr, item);
        value_release(&item);
      }
      *out = arr;
      return true;
    }
    case N_OBJECT: {
      Value obj = v_obj();
      for (int i = 0; i < n->propcount; i++) {
        Value v;
        if (!eval_node(in, env, n->props[i].value, &v)) {
          value_release(&obj);
          return false;
        }
        obj_set(&obj, n->props[i].key, v);
        value_release(&v);
      }
      *out = obj;
      return true;
    }
    case N_FUNC: {
      Value f = v_fun(n, env, n->name);
      *out = f;
      return true;
    }
    default:
      break;
  }
  interp_error(in, n->line, n->col, "E212", "Cannot use this value here");
  return false;
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static ExecResult exec_stmt(Interp* in, Env* env, Node* n) {
  if (!n) return EX_NORMAL;
  if (in->failed) return EX_NORMAL;

  switch (n->kind) {
    case N_VAR: {
      Value v = v_null();
      if (n->right) {
        if (!eval_node(in, env, n->right, &v)) return EX_NORMAL;
      }
      env_define(env, n->name, v);
      value_release(&v);
      return EX_NORMAL;
    }
    case N_EXPR: {
      Value v;
      if (!eval_node(in, env, n->left, &v)) return EX_NORMAL;
      value_release(&v);
      return EX_NORMAL;
    }
    case N_PRINT: {
      Value v;
      if (!eval_node(in, env, n->left, &v)) return EX_NORMAL;
      char* s = value_to_string(v);
      printf("[easyweb] %s\n", s);
      fflush(stdout);
      free(s);
      value_release(&v);
      return EX_NORMAL;
    }
    case N_IF: {
      Value c;
      if (!eval_node(in, env, n->cond, &c)) return EX_NORMAL;
      bool t = value_truthy(c);
      value_release(&c);
      if (t) return exec_stmt(in, env, n->body);
      if (n->elseBody) return exec_stmt(in, env, n->elseBody);
      return EX_NORMAL;
    }
    case N_WHILE: {
      for (;;) {
        Value c;
        if (!eval_node(in, env, n->cond, &c)) return EX_NORMAL;
        bool t = value_truthy(c);
        value_release(&c);
        if (!t) break;
        ExecResult r = exec_stmt(in, env, n->body);
        if (r == EX_BREAK) break;
        if (r == EX_RETURN) return r;
      }
      return EX_NORMAL;
    }
    case N_FOR: {
      Env* loop = env_new(env);
      if (n->init) {
        ExecResult r = exec_stmt(in, loop, n->init);
        if (in->failed) { env_free(loop); return EX_NORMAL; }
        if (r != EX_NORMAL) { env_free(loop); return r; }
      }
      for (;;) {
        if (n->cond) {
          Value c;
          if (!eval_node(in, loop, n->cond, &c)) { env_free(loop); return EX_NORMAL; }
          bool t = value_truthy(c);
          value_release(&c);
          if (!t) break;
        }
        ExecResult r = exec_stmt(in, loop, n->body);
        if (r == EX_BREAK) break;
        if (r == EX_RETURN) { env_free(loop); return r; }
        if (n->inc) {
          Value v;
          if (!eval_node(in, loop, n->inc->left, &v)) { env_free(loop); return EX_NORMAL; }
          value_release(&v);
        }
      }
      env_free(loop);
      return EX_NORMAL;
    }
    case N_FORIN: {
      Value coll;
      if (!eval_node(in, env, n->right, &coll)) return EX_NORMAL;
      Env* loop = env_new(env);
      size_t count = coll.k == V_ARR ? arr_len(&coll) : (coll.k == V_OBJ ? obj_count(&coll) : 0);
      for (size_t i = 0; i < count; i++) {
        Value item = v_null();
        if (coll.k == V_ARR) {
          item = arr_get(&coll, i);
        } else if (coll.k == V_OBJ) {
          char** keys = obj_keys(&coll);
          item = v_str(keys[i]);
          for (int k = 0; keys[k]; k++) free(keys[k]);
          free(keys);
        }
        env_define(loop, n->name, item);
        value_release(&item);
        ExecResult r = exec_stmt(in, loop, n->body);
        if (r == EX_BREAK) break;
        if (r == EX_RETURN) { env_free(loop); value_release(&coll); return r; }
      }
      env_free(loop);
      value_release(&coll);
      return EX_NORMAL;
    }
    case N_RETURN: {
      Value v = v_null();
      if (n->left) {
        if (!eval_node(in, env, n->left, &v)) return EX_NORMAL;
      }
      in->retval = v;
      in->has_ret = true;
      return EX_RETURN;
    }
    case N_BREAK:
      return EX_BREAK;
    case N_CONTINUE:
      return EX_CONTINUE;
    case N_BLOCK: {
      Env* scope = env_new(env);
      for (int i = 0; i < n->count; i++) {
        ExecResult r = exec_stmt(in, scope, n->items[i]);
        if (r != EX_NORMAL) {
          env_free(scope);
          return r;
        }
      }
      env_free(scope);
      return EX_NORMAL;
    }
    case N_TRY: {
      bool was_failed = in->failed;
      Diag saved = in->diag;
      in->failed = false;
      Env* scope = env_new(env);
      ExecResult r = EX_NORMAL;
      for (int i = 0; i < n->body->count; i++) {
        r = exec_stmt(in, scope, n->body->items[i]);
        if (r != EX_NORMAL) break;
      }
      env_free(scope);
      if (in->failed) {
        in->failed = was_failed;
        in->diag = saved;
        if (n->elseBody) {
          Env* cscope = env_new(env);
          if (n->name) {
            Value ev = v_str("something went wrong");
            env_define(cscope, n->name, ev);
            value_release(&ev);
          }
          ExecResult cr = EX_NORMAL;
          for (int i = 0; i < n->elseBody->count; i++) {
            cr = exec_stmt(in, cscope, n->elseBody->items[i]);
            if (cr != EX_NORMAL) break;
          }
          env_free(cscope);
          return cr;
        }
      }
      return r;
    }
    case N_THROW: {
      Value v;
      if (!eval_node(in, env, n->left, &v)) return EX_NORMAL;
      char* s = value_to_string(v);
      interp_error(in, n->line, n->col, "E213", "%s", s);
      free(s);
      value_release(&v);
      return EX_NORMAL;
    }
    case N_FUNC: {
      Value f = v_fun(n, env, n->name);
      if (n->name) {
        env_define(env, n->name, f);
      } else {
        env_define(env, "__anon", f);
      }
      value_release(&f);
      return EX_NORMAL;
    }
    default:
      break;
  }
  /* expression used as a statement */
  {
    Value v;
    if (!eval_node(in, env, n, &v)) return EX_NORMAL;
    value_release(&v);
  }
  return EX_NORMAL;
}

/* ------------------------------------------------------------------ */
/* loading                                                             */
/* ------------------------------------------------------------------ */

static bool interp_run_block(Interp* in, Env* env, Node* block) {
  if (!block) return true;
  for (int i = 0; i < block->count; i++) {
    ExecResult r = exec_stmt(in, env, block->items[i]);
    if (in->failed) return false;
    if (r == EX_RETURN) break;
  }
  return true;
}

static void interp_add_script(Interp* in, Node* block) {
  if (in->nscript == in->script_cap) {
    in->script_cap = in->script_cap ? in->script_cap * 2 : 4;
    in->script_blocks = (Node**)ew_realloc(in->script_blocks, sizeof(Node*) * (size_t)in->script_cap);
  }
  in->script_blocks[in->nscript++] = block;
}

static void interp_add_client_func(Interp* in, Node* fn) {
  if (!fn || !fn->name) return;
  if (in->nclient_funcs == in->client_funcs_cap) {
    in->client_funcs_cap = in->client_funcs_cap ? in->client_funcs_cap * 2 : 8;
    in->client_funcs = (Node**)ew_realloc(in->client_funcs, sizeof(Node*) * (size_t)in->client_funcs_cap);
  }
  in->client_funcs[in->nclient_funcs++] = fn;
}

static void interp_keep_ast(Interp* in, Node* n) {
  if (!n) return;
  if (in->npkg_progs == in->pkg_progs_cap) {
    in->pkg_progs_cap = in->pkg_progs_cap ? in->pkg_progs_cap * 2 : 4;
    in->pkg_progs = (Node**)ew_realloc(in->pkg_progs, sizeof(Node*) * (size_t)in->pkg_progs_cap);
  }
  in->pkg_progs[in->npkg_progs++] = n;
}

static bool interp_load_package(Interp* in, const char* name) {
  PackageList* installed = pkg_installed(in->app_dir);
  PackageList* reg = NULL;
  Package* p = pkg_find(installed, name);
  if (!p) {
    reg = pkg_builtin_registry();
    p = pkg_find(reg, name);
  }
  if (!p) {
    interp_error(in, 0, 0, "E220", "Package '%s' is not installed", name);
    diag_hint(&in->diag, "Install it with: webhelp install %s", name);
    pkg_list_free(installed);
    pkg_list_free(reg);
    return false;
  }

  size_t len = 0;
  char* src = pkg_load_entry(p, &len);
  if (src) {
    Parser pp;
    parser_init(&pp, src, len, name, in->kid);
    Node* prog = parser_parse(&pp);
    if (!prog) {
      interp_error(in, 0, 0, "E221", "Package '%s' has a syntax problem", name);
      in->diag = pp.diag;
      free(src);
      pkg_list_free(installed);
      pkg_list_free(reg);
      return false;
    }
    /* the package runs in the global scope so its names (db, animate, ...)
       are usable from the app */
    interp_run_block(in, in->globals, prog);
    for (int i = 0; i < prog->count; i++) {
      Node* fn = prog->items[i];
      if (fn->kind == N_FUNC && fn->name && fn->bval) {
        char* js = jsgen_func(fn);
        buf_puts(&in->pkg_func_js, js);
        buf_puts(&in->pkg_func_js, "\n");
        free(js);
      }
    }
    /* the package tree must stay alive - its functions point into it */
    interp_keep_ast(in, prog);
    free(src);
  }

  size_t clen = 0;
  char* css = pkg_load_asset(p, "css", &clen);
  if (css) {
    buf_puts(&in->css, css);
    buf_puts(&in->css, "\n");
    free(css);
  }
  size_t jlen = 0;
  char* js = pkg_load_asset(p, "js", &jlen);
  if (js) {
    buf_puts(&in->client_js, js);
    buf_puts(&in->client_js, "\n");
    free(js);
  }
  pkg_list_free(installed);
  pkg_list_free(reg);
  return true;
}

bool interp_load_file(Interp* in, const char* path, Diag* out) {
  char* src = NULL;
  size_t len = 0;
  if (!path_read_file(path, &src, &len)) {
    diag_set(out, path, 0, 0, "E100", "Cannot open file '%s'", path);
    return false;
  }
  in->app_path = ew_strdup(path);
  {
    char* dir = path_dirname(path);
    char* cwd = path_cwd();
    char* abs;
    if (strcmp(dir, ".") == 0 || strcmp(dir, "") == 0) {
      abs = ew_strdup(cwd);
    } else {
      abs = path_join(cwd, dir);
    }
    in->app_dir = path_abs(abs);
    free(dir);
    free(cwd);
    free(abs);
  }
  char* base = path_basename(path);
  char* stem = path_strip_ext(base);
  in->app_name = ew_strdup(stem);
  free(base);
  free(stem);

  Parser p;
  parser_init(&p, src, len, path, in->kid);
  Node* prog = parser_parse(&p);
  if (!prog) {
    *out = p.diag;
    free(src);
    return false;
  }
  in->root = prog;

  /* pass 1: load packages */
  for (int i = 0; i < prog->count; i++) {
    Node* s = prog->items[i];
    if (s->kind == N_USE) {
      if (!interp_load_package(in, s->name)) {
        *out = in->diag;
        free(src);
        return false;
      }
    }
  }
  /* pass 2: run the program */
  for (int i = 0; i < prog->count; i++) {
    Node* s = prog->items[i];
    switch (s->kind) {
      case N_USE:
        break;
      case N_ROUTE:
        route_add(in, s->name, s->name2, s->body);
        break;
      case N_PAGE:
        free(in->page_title);
        in->page_title = ew_strdup(s->str ? s->str : "easyweb app");
        break;
      case N_PORT:
        in->port = (int)s->num;
        break;
      case N_STYLE:
        buf_puts(&in->css, s->str ? s->str : "");
        buf_puts(&in->css, "\n");
        break;
      case N_SCRIPT:
        interp_add_script(in, s->body);
        break;
      case N_DATA: {
        Value v;
        if (!eval_node(in, in->globals, s->left, &v)) {
          *out = in->diag;
          free(src);
          return false;
        }
        env_define(in->globals, "data", v);
        value_release(&v);
        in->data_defined = true;
        break;
      }
      case N_UI:
        free(in->ui_raw);
        in->ui_raw = ew_strdup(s->str ? s->str : "");
        break;
      case N_KID:
        free(in->kid_raw);
        in->kid_raw = ew_strdup(s->str ? s->str : "");
        break;
      case N_KIDMODE:
        in->kid = s->bval;
        free(in->kidmode);
        in->kidmode = ew_strdup(s->bval ? "on" : "off");
        break;
      case N_FUNC: {
        Value f = v_fun(s, in->globals, s->name);
        if (!s->bval) {
          /* client functions live in the browser only */
          if (s->name) env_define(in->globals, s->name, f);
        }
        interp_add_client_func(in, s);
        value_release(&f);
        break;
      }
      default: {
        ExecResult r = exec_stmt(in, in->globals, s);
        if (in->failed) {
          *out = in->diag;
          free(src);
          return false;
        }
        (void)r;
        break;
      }
    }
  }
  free(src);
  return true;
}

bool interp_call_route(Interp* in, const Route* rt, Env* env, Value* result) {
  in->has_ret = false;
  in->retval = v_null();
  for (int i = 0; i < rt->handler->count; i++) {
    ExecResult r = exec_stmt(in, env, rt->handler->items[i]);
    if (in->failed) return false;
    if (r == EX_RETURN) break;
  }
  if (in->has_ret) {
    *result = in->retval;
    in->retval = v_null();
    in->has_ret = false;
  } else {
    *result = v_null();
  }
  return true;
}

bool interp_eval(Interp* in, Env* env, Node* n, Value* out) {
  return eval_node(in, env, n, out);
}
