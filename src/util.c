/* easyweb - buffers, paths, diagnostics. */
#include "ew.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define EW_MKDIR(p) _mkdir(p)
#define EW_GETCWD _getcwd
#define EW_SEP '\\'
#else
#include <unistd.h>
#include <dirent.h>
#define EW_MKDIR(p) mkdir(p, 0777)
#define EW_GETCWD getcwd
#define EW_SEP '/'
#endif

/* ------------------------------------------------------------------ */
/* allocation                                                          */
/* ------------------------------------------------------------------ */

void* ew_malloc(size_t n) {
  void* p = malloc(n ? n : 1);
  if (!p) {
    fprintf(stderr, "easyweb: out of memory\n");
    exit(70);
  }
  return p;
}

void* ew_calloc(size_t n, size_t sz) {
  void* p = calloc(n ? n : 1, sz ? sz : 1);
  if (!p) {
    fprintf(stderr, "easyweb: out of memory\n");
    exit(70);
  }
  return p;
}

void* ew_realloc(void* p, size_t n) {
  void* q = realloc(p, n ? n : 1);
  if (!q) {
    fprintf(stderr, "easyweb: out ofmemory\n");
    exit(70);
  }
  return q;
}

char* ew_strdup(const char* s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char* d = (char*)ew_malloc(n + 1);
  memcpy(d, s, n + 1);
  return d;
}

char* ew_strndup(const char* s, size_t n) {
  char* d = (char*)ew_malloc(n + 1);
  memcpy(d, s, n);
  d[n] = 0;
  return d;
}

char* ew_asprintf(const char* fmt, ...) {
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) n = 0;
  char* out = (char*)ew_malloc((size_t)n + 1);
  vsnprintf(out, (size_t)n + 1, fmt, ap2);
  va_end(ap2);
  return out;
}

/* ------------------------------------------------------------------ */
/* buffer                                                              */
/* ------------------------------------------------------------------ */

void buf_init(Buf* b) {
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

void buf_free(Buf* b) {
  free(b->data);
  buf_init(b);
}

void buf_reserve(Buf* b, size_t extra) {
  if (b->len + extra + 1 <= b->cap) return;
  size_t cap = b->cap ? b->cap : 256;
  while (cap < b->len + extra + 1) cap *= 2;
  b->data = (char*)ew_realloc(b->data, cap);
  b->cap = cap;
}

void buf_putc(Buf* b, char c) {
  buf_reserve(b, 1);
  b->data[b->len++] = c;
  b->data[b->len] = 0;
}

void buf_putn(Buf* b, const char* s, size_t n) {
  if (!s || !n) return;
  buf_reserve(b, n);
  memcpy(b->data + b->len, s, n);
  b->len += n;
  b->data[b->len] = 0;
}

void buf_puts(Buf* b, const char* s) {
  if (s) buf_putn(b, s, strlen(s));
}

void buf_printf(Buf* b, const char* fmt, ...) {
  va_list ap, ap2;
  va_start(ap, fmt);
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n <= 0) {
    va_end(ap2);
    return;
  }
  buf_reserve(b, (size_t)n);
  vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
  va_end(ap2);
  b->len += (size_t)n;
}

char* buf_release(Buf* b) {
  char* d = b->data ? b->data : ew_strdup("");
  buf_init(b);
  return d;
}

/* ------------------------------------------------------------------ */
/* paths                                                               */
/* ------------------------------------------------------------------ */

static bool path_is_sep(char c) {
#ifdef _WIN32
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

char* path_join(const char* a, const char* b) {
  if (!a || !*a) return ew_strdup(b ? b : "");
  if (!b || !*b) return ew_strdup(a);
  size_t la = strlen(a);
  bool need_sep = !path_is_sep(a[la - 1]);
  return ew_asprintf("%s%s%s", a, need_sep ? "/" : "", b);
}

char* path_dirname(const char* p) {
  if (!p) return ew_strdup(".");
  size_t n = strlen(p);
  while (n > 1 && path_is_sep(p[n - 1])) n--;
  size_t i = n;
  while (i > 0 && !path_is_sep(p[i - 1])) i--;
  if (i == 0) return ew_strdup(".");
  while (i > 1 && path_is_sep(p[i - 1])) i--;
  return ew_strndup(p, i);
}

char* path_basename(const char* p) {
  if (!p) return ew_strdup("");
  size_t n = strlen(p);
  while (n > 1 && path_is_sep(p[n - 1])) n--;
  size_t i = n;
  while (i > 0 && !path_is_sep(p[i - 1])) i--;
  return ew_strndup(p + i, n - i);
}

char* path_ext(const char* p) {
  if (!p) return ew_strdup("");
  char* b = path_basename(p);
  char* dot = strrchr(b, '.');
  char* r = ew_strdup(dot ? dot + 1 : "");
  free(b);
  return r;
}

char* path_strip_ext(const char* p) {
  if (!p) return ew_strdup("");
  char* b = path_basename(p);
  char* dot = strrchr(b, '.');
  if (dot) *dot = 0;
  char* d = path_dirname(p);
  char* res;
  if (!d || !strcmp(d, ".")) {
    res = ew_strdup(b);
  } else {
    res = path_join(d, b);
  }
  free(b);
  free(d);
  return res;
}

bool path_exists(const char* p) {
  struct stat st;
  return p && stat(p, &st) == 0;
}

bool path_is_dir(const char* p) {
  struct stat st;
  return p && stat(p, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}

bool path_mkdir_p(const char* p) {
  if (!p || !*p) return false;
  if (path_is_dir(p)) return true;
  char* copy = ew_strdup(p);
  char* cur = copy;
  bool ok = true;
  for (char* q = copy + 1; *q; q++) {
    if (path_is_sep(*q)) {
      *q = 0;
      if (!path_is_dir(cur)) {
        if (EW_MKDIR(cur) != 0 && errno != EEXIST) {
          ok = false;
          break;
        }
      }
      *q = '/';
    }
  }
  if (ok && !path_is_dir(cur)) {
    if (EW_MKDIR(cur) != 0 && errno != EEXIST) ok = false;
  }
  free(copy);
  return ok;
}

bool path_read_file(const char* p, char** out, size_t* out_len) {
  FILE* f = fopen(p, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return false;
  }
  fseek(f, 0, SEEK_SET);
  char* buf = (char*)ew_malloc((size_t)sz + 1);
  size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[got] = 0;
  *out = buf;
  if (out_len) *out_len = got;
  return true;
}

bool path_write_file(const char* p, const char* data, size_t len) {
  FILE* f = fopen(p, "wb");
  if (!f) return false;
  size_t w = len ? fwrite(data, 1, len, f) : 0;
  fclose(f);
  return w == len;
}

bool path_remove(const char* p) {
  return remove(p) == 0;
}

char* path_cwd(void) {
  char buf[4096];
  if (EW_GETCWD(buf, sizeof(buf))) return ew_strdup(buf);
  return ew_strdup(".");
}

char* path_home(void) {
  const char* h = getenv("HOME");
  if (!h || !*h) {
#ifdef _WIN32
    h = getenv("USERPROFILE");
#endif
  }
  if (!h || !*h) h = ".";
  return ew_strdup(h);
}

char* path_abs(const char* p) {
  if (!p) return path_cwd();
  if (p[0] == '/' || p[0] == '\\') return ew_strdup(p);
#ifdef _WIN32
  if (strlen(p) > 2 && p[1] == ':') return ew_strdup(p);
#endif
  char* cwd = path_cwd();
  char* res = path_join(cwd, p);
  free(cwd);
  return res;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */

static bool g_ansi = false;

void ew_print_ansi(bool on) {
  g_ansi = on;
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(h, &mode)) {
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
  h = GetStdHandle(STD_OUTPUT_HANDLE);
  mode = 0;
  if (GetConsoleMode(h, &mode)) {
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#endif
}

bool diag_ansi_enabled(void) {
  return g_ansi;
}

void diag_init(Diag* d) {
  memset(d, 0, sizeof(*d));
  d->line = 0;
  d->col = 0;
}

void diag_free(Diag* d) {
  diag_init(d);
}

void diag_set(Diag* d, const char* file, int line, int col, const char* code, const char* fmt, ...) {
  d->file = file;
  d->line = line;
  d->col = col;
  snprintf(d->code, sizeof(d->code), "%s", code ? code : "E000");
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d->message, sizeof(d->message), fmt, ap);
  va_end(ap);
}

void diag_hint(Diag* d, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(d->hint, sizeof(d->hint), fmt, ap);
  va_end(ap);
}

static void print_source_line(const char* file, int line, int col, bool color) {
  if (!file || line <= 0) return;
  char* src = NULL;
  size_t len = 0;
  if (!path_read_file(file, &src, &len)) return;
  int cur = 1;
  const char* end = src + len;
  const char* lstart = src;
  while (cur < line && lstart < end) {
    const char* nl = memchr(lstart, '\n', (size_t)(end - lstart));
    if (!nl) break;
    lstart = nl + 1;
    cur++;
  }
  const char* lend = lstart;
  while (lend < end && *lend != '\n') lend++;
  size_t llen = (size_t)(lend - lstart);
  /* trim \r */
  while (llen > 0 && lstart[llen - 1] == '\r') llen--;

  Buf b;
  buf_init(&b);
  buf_printf(&b, "%s%4d | %s\n", color ? "\033[90m" : "", line, color ? "\033[0m" : "");
  buf_putn(&b, lstart, llen);
  buf_puts(&b, "\n");
  buf_printf(&b, "%s     | ", color ? "\033[90m" : "");
  buf_puts(&b, color ? "\033[0m" : "");
  int c = col > 0 ? col : 1;
  for (int i = 1; i < c; i++) buf_putc(&b, ' ');
  buf_puts(&b, color ? "\033[31m" : "");
  buf_puts(&b, "^");
  buf_puts(&b, color ? "\033[0m" : "");
  buf_puts(&b, "\n");
  fputs(b.data ? b.data : "", stderr);
  buf_free(&b);
  free(src);
}

void diag_print(const Diag* d, bool color) {
  const char* label = "error";
  const char* col = color ? "\033[1;31m" : "";
  const char* reset = color ? "\033[0m" : "";
  const char* dim = color ? "\033[90m" : "";
  const char* loc = "";
  if (d->file) loc = d->line > 0 ? ew_asprintf("%s:%d:%d", d->file, d->line, d->col) : ew_asprintf("%s", d->file);
  else loc = ew_strdup("");
  fprintf(stderr, "%s%s%s[%s]%s: %s\n", col, label, reset, d->code, reset, d->message);
  fprintf(stderr, "  %s%s%s\n", dim, loc, reset);
  free((void*)loc);
  if (d->line > 0) print_source_line(d->file, d->line, d->col, color);
  if (d->hint[0]) fprintf(stderr, "  %shelp:%s %s\n", dim, reset, d->hint);
}

void diag_print_friendly(const Diag* d, bool color) {
  const char* col = color ? "\033[1;33m" : "";
  const char* reset = color ? "\033[0m" : "";
  const char* dim = color ? "\033[90m" : "";
  fprintf(stderr, "%sOops! Your program has a little problem%s\n", col, reset);
  fprintf(stderr, "  %sWhat happened:%s %s\n", dim, reset, d->message);
  if (d->hint[0]) fprintf(stderr, "  %sTry this:%s %s\n", dim, reset, d->hint);
  if (d->file && d->line > 0)
    fprintf(stderr, "  %sIt is on line %d, column %d%s\n", dim, d->line, d->col, reset);
  fprintf(stderr, "  %sTip: ask a grown-up or check the easyweb guide!%s\n", dim, reset);
}
