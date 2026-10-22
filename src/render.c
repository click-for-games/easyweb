/* easyweb - page rendering: templates, styles and client scripts. */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

static const char* BASE_CSS =
    "*{box-sizing:border-box}"
    "body{margin:0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;background:#0f1117;color:#e8eaf0;line-height:1.6}"
    "a{color:#7cc4ff}"
    ".ew-wrap{max-width:900px;margin:0 auto;padding:32px 20px}"
    ".ew-toast{position:fixed;left:50%;bottom:28px;transform:translateX(-50%);background:#2b3040;color:#fff;"
    "padding:12px 20px;border-radius:12px;box-shadow:0 10px 30px rgba(0,0,0,.45);z-index:9999;"
    "animation:ewToastIn .25s ease both}"
    ".ew-toast.ew-toast-out{opacity:0;transform:translate(-50%,10px);transition:.3s}"
    "@keyframes ewToastIn{from{opacity:0;transform:translate(-50%,14px)}to{opacity:1;transform:translate(-50%,0)}}";

static const char* KID_CSS =
    ".ew-kid{background:#111827;border-radius:20px;padding:24px;margin:18px 0;border:2px solid #374151}"
    ".ew-kid-row{display:flex;align-items:center;gap:12px;margin:12px 0;flex-wrap:wrap}"
    ".ew-kid-button{font-size:20px;padding:14px 24px;border-radius:14px;border:0;cursor:pointer;"
    "background:linear-gradient(135deg,#f9a825,#f44336);color:#fff;box-shadow:0 6px 0 #b23a2e;transition:.12s}"
    ".ew-kid-button:active{transform:translateY(4px);box-shadow:0 2px 0 #b23a2e}"
    ".ew-kid-count{font-size:18px;font-weight:700;color:#fdd835;min-width:28px}"
    ".ew-kid-input{font-size:18px;padding:12px 14px;border-radius:12px;border:2px solid #374151;background:#1f2937;color:#fff}"
    ".ew-kid-slider{width:220px;accent-color:#f9a825}"
    ".ew-kid-value{font-size:18px;color:#fdd835;font-weight:700}"
    ".ew-kid-box{font-size:20px;padding:16px;background:#1f2937;border-radius:12px;margin:8px 0}"
    ".ew-kid-image{max-width:220px;border-radius:12px}"
    ".ew-kid-link{font-size:20px}";

static char* escape_html(const char* s) {
  Buf b;
  buf_init(&b);
  for (const char* p = s ? s : ""; *p; p++) {
    switch (*p) {
      case '&': buf_puts(&b, "&amp;"); break;
      case '<': buf_puts(&b, "&lt;"); break;
      case '>': buf_puts(&b, "&gt;"); break;
      case '"': buf_puts(&b, "&quot;"); break;
      case '\'': buf_puts(&b, "&#39;"); break;
      default: buf_putc(&b, *p);
    }
  }
  return buf_release(&b);
}

/* Renders {{ expr }} and {{{ raw }}} inside a template. */
char* render_template(const char* raw, Interp* in, Env* env, bool* ok) {
  Buf out;
  buf_init(&out);
  if (!raw) {
    if (ok) *ok = true;
    return buf_release(&out);
  }
  const char* p = raw;
  while (*p) {
    if (p[0] == '{' && p[1] == '{') {
      bool raw_mode = (p[2] == '{');
      const char* start = p + (raw_mode ? 3 : 2);
      const char* close = raw_mode ? strstr(start, "}}}") : strstr(start, "}}");
      if (!close) {
        buf_putc(&out, *p);
        p++;
        continue;
      }
      size_t elen = (size_t)(close - start);
      char* expr = ew_strndup(start, elen);
      /* trim */
      char* e = expr;
      while (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r') e++;
      char* end = expr + strlen(expr);
      while (end > e && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) *--end = 0;

      Parser p2;
      Node* node = parser_parse_expr(&p2, e, strlen(e), in->app_path);
      if (!node) {
        if (ok) *ok = false;
        free(expr);
        buf_free(&out);
        return NULL;
      }
      Value v;
      bool good = interp_eval(in, env, node, &v);
      node_free(node);
      free(expr);
      if (!good) {
        if (ok) *ok = false;
        buf_free(&out);
        return NULL;
      }
      char* s = value_to_string(v);
      if (raw_mode) {
        buf_puts(&out, s);
      } else {
        char* e2 = escape_html(s);
        buf_puts(&out, e2);
        free(e2);
      }
      free(s);
      value_release(&v);
      p = close + (raw_mode ? 3 : 2);
      continue;
    }
    buf_putc(&out, *p);
    p++;
  }
  if (ok) *ok = true;
  return buf_release(&out);
}

/* ------------------------------------------------------------------ */
/* client script                                                       */
/* ------------------------------------------------------------------ */

char* jsgen_program(Interp* in) {
  Buf b;
  buf_init(&b);
  buf_puts(&b, ew_client_runtime_js());
  buf_puts(&b, in->client_js.data ? in->client_js.data : "");
  buf_puts(&b, "\n(function(){\n");
  for (int i = 0; i < in->nclient_funcs; i++) {
    char* js = jsgen_func(in->client_funcs[i]);
    buf_puts(&b, js);
    buf_puts(&b, "\n");
    free(js);
  }
  if (in->pkg_func_js.data) {
    buf_puts(&b, in->pkg_func_js.data);
  }
  for (int i = 0; i < in->nscript; i++) {
    Node* blk = in->script_blocks[i];
    for (int j = 0; j < blk->count; j++) {
      char* js = jsgen_func(blk->items[j]);
      buf_puts(&b, js);
      buf_puts(&b, "\n");
      free(js);
    }
  }
  buf_puts(&b, "})();\n");
  return buf_release(&b);
}

/* ------------------------------------------------------------------ */
/* page                                                                */
/* ------------------------------------------------------------------ */

char* render_page(Interp* in, Env* env) {
  Buf b;
  buf_init(&b);
  buf_puts(&b, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n");
  buf_puts(&b, "<meta charset=\"utf-8\">\n");
  buf_puts(&b, "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n");
  buf_puts(&b, "<meta name=\"generator\" content=\"easyweb " EW_VERSION "\">\n");
  buf_puts(&b, "<title>");
  char* title = escape_html(in->page_title ? in->page_title : "easyweb app");
  buf_puts(&b, title);
  free(title);
  buf_puts(&b, "</title>\n<style>\n");
  buf_puts(&b, BASE_CSS);
  buf_puts(&b, "\n");
  if (in->kid) buf_puts(&b, KID_CSS);
  buf_puts(&b, "\n");
  buf_puts(&b, in->css.data ? in->css.data : "");
  buf_puts(&b, "\n</style>\n</head>\n<body>\n<div class=\"ew-wrap\">\n");

  if (in->ui_raw) {
    bool ok = true;
    char* html = render_template(in->ui_raw, in, env, &ok);
    if (html) {
      buf_puts(&b, html);
      free(html);
    }
  }

  if (in->kid_raw) {
    Diag d;
    diag_init(&d);
    KidBlock* kb = kid_parse(in->kid_raw, in->app_path, &d);
    char* html = kid_render_html(kb, in->kid);
    buf_puts(&b, html);
    free(html);
    char* js = kid_render_js(kb);
    buf_printf(&b, "<script>\n%s</script>\n", js);
    free(js);
    kid_free(kb);
    diag_free(&d);
  }

  buf_puts(&b, "\n</div>\n");
  char* js = jsgen_program(in);
  buf_printf(&b, "<script>\n%s</script>\n", js);
  free(js);
  buf_puts(&b, "</body>\n</html>\n");
  return buf_release(&b);
}
