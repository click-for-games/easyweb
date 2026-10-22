/* easyweb - kid mode: a tiny, friendly UI language.
 *
 *   KID {
 *     BUTTON VAR _ TEXT <hey click me!>
 *     INPUT VAR name PLACEHOLDER <your name>
 *   }
 */
#include "ew.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char* kid_trim(const char* s) {
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  const char* e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
  return ew_strndup(s, (size_t)(e - s));
}

static void kid_push(KidBlock* kb, KidElem e) {
  if (kb->count == kb->cap) {
    kb->cap = kb->cap ? kb->cap * 2 : 8;
    kb->items = (KidElem*)ew_realloc(kb->items, sizeof(KidElem) * (size_t)kb->cap);
  }
  kb->items[kb->count++] = e;
}

void kid_free(KidBlock* kb) {
  if (!kb) return;
  for (int i = 0; i < kb->count; i++) {
    free(kb->items[i].tag);
    free(kb->items[i].var);
    free(kb->items[i].text);
    free(kb->items[i].id);
    free(kb->items[i].action);
    free(kb->items[i].placeholder);
    free(kb->items[i].src);
    free(kb->items[i].url);
  }
  free(kb->items);
  free(kb);
}

/* splits a line into tokens; <...> keeps spaces */
static char** kid_tokenize(const char* line, int* out_count) {
  char** toks = NULL;
  int count = 0, cap = 0;
  const char* p = line;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!*p) break;
    char* tok = NULL;
    if (*p == '<') {
      p++;
      const char* start = p;
      while (*p && *p != '>') p++;
      tok = ew_strndup(start, (size_t)(p - start));
      if (*p == '>') p++;
    } else {
      const char* start = p;
      while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
      tok = ew_strndup(start, (size_t)(p - start));
    }
    if (count == cap) {
      cap = cap ? cap * 2 : 8;
      toks = (char**)ew_realloc(toks, sizeof(char*) * (size_t)cap);
    }
    toks[count++] = tok;
  }
  *out_count = count;
  return toks;
}

static bool kid_is(const char* s, const char* what) {
  while (*s && *what) {
    if (toupper((unsigned char)*s) != toupper((unsigned char)*what)) return false;
    s++;
    what++;
  }
  return *s == 0 && *what == 0;
}

KidBlock* kid_parse(const char* raw, const char* file, Diag* d) {
  KidBlock* kb = (KidBlock*)ew_calloc(1, sizeof(KidBlock));
  if (!raw) return kb;

  int lineno = 0;
  int counter = 0;
  const char* p = raw;
  char* line = NULL;
  while (*p || (line && lineno == 0)) {
    const char* nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    char* cur = ew_strndup(p, len);
    lineno++;
    if (lineno > 1 || *cur) {
      char* t = kid_trim(cur);
      if (*t && strcmp(t, "}") != 0) {
        int ntok = 0;
        char** toks = kid_tokenize(t, &ntok);
        if (ntok == 0) {
          free(t);
          free(cur);
          if (nl) p = nl + 1; else break;
          continue;
        }
        KidElem e;
        memset(&e, 0, sizeof(e));
        e.line = lineno;
        e.tag = ew_strdup(toks[0]);
        for (int i = 0; i < ntok; i++) free(toks[i]);
        free(toks);

        counter++;
        e.id = ew_asprintf("ew_kid_%d", counter);

        /* key/value pairs, with friendly shorthands */
        char** kt = kid_tokenize(t, &ntok);
        for (int i = 1; i < ntok; i++) {
          const char* k = kt[i];
          const char* v = NULL;
          if (kid_is(k, "VAR") || kid_is(k, "TEXT") || kid_is(k, "SRC") || kid_is(k, "URL") ||
              kid_is(k, "PLACEHOLDER") || kid_is(k, "ACTION") || kid_is(k, "ID") ||
              kid_is(k, "MIN") || kid_is(k, "MAX") || kid_is(k, "VALUE")) {
            if (i + 1 >= ntok) break;
            v = kt[++i];
          } else if (strcmp(k, "_") == 0) {
            /* "_" asks easyweb to pick a name */
            if (!e.var) e.var = ew_strdup("_");
            continue;
          } else {
            /* a bare word is the text of the block */
            if (!e.text) e.text = ew_strdup(k);
            continue;
          }
          if (kid_is(k, "VAR")) {
            if (!e.var) e.var = ew_strdup(v);
          } else if (kid_is(k, "TEXT")) {
            if (!e.text) e.text = ew_strdup(v);
          } else if (kid_is(k, "SRC")) {
            if (!e.src) e.src = ew_strdup(v);
          } else if (kid_is(k, "URL")) {
            if (!e.url) e.url = ew_strdup(v);
          } else if (kid_is(k, "PLACEHOLDER")) {
            if (!e.placeholder) e.placeholder = ew_strdup(v);
          } else if (kid_is(k, "ACTION")) {
            if (!e.action) e.action = ew_strdup(v);
          } else if (kid_is(k, "ID")) {
            free(e.id);
            e.id = ew_strdup(v);
          } else if (kid_is(k, "MIN")) {
            e.min = atof(v);
            e.has_min = true;
          } else if (kid_is(k, "MAX")) {
            e.max = atof(v);
            e.has_max = true;
          } else if (kid_is(k, "VALUE")) {
            e.value = atof(v);
            e.has_value = true;
          }
        }
        for (int i = 0; i < ntok; i++) free(kt[i]);
        free(kt);

        /* auto variable name */
        if (!e.var || strcmp(e.var, "_") == 0) {
          char* prefix = ew_asprintf("ew_%s", e.tag);
          for (char* c = prefix; *c; c++) *c = (char)tolower((unsigned char)*c);
          free(e.var);
          e.var = ew_asprintf("%s_%d", prefix, counter);
          free(prefix);
        }
        if (!e.text) e.text = ew_strdup("");

        kid_push(kb, e);
      }
      free(t);
    }
    free(cur);
    if (!nl) break;
    p = nl + 1;
  }
  free(line);
  (void)file;
  (void)d;
  return kb;
}

static char* html_escape(const char* s) {
  Buf b;
  buf_init(&b);
  for (const char* p = s; p && *p; p++) {
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

char* kid_render_html(KidBlock* kb, bool kidmode) {
  (void)kidmode;
  Buf b;
  buf_init(&b);
  if (!kb) return buf_release(&b);
  buf_puts(&b, "<div class=\"ew-kid\">\n");
  for (int i = 0; i < kb->count; i++) {
    KidElem* e = &kb->items[i];
    char* text = html_escape(e->text);
    if (kid_is(e->tag, "BUTTON")) {
      buf_printf(&b, "  <div class=\"ew-kid-row\">\n");
      buf_printf(&b, "    <button type=\"button\" id=\"%s\" class=\"ew-kid-button\" data-var=\"%s\">%s</button>\n",
                 e->id, e->var, text);
      buf_printf(&b, "    <span class=\"ew-kid-count\" id=\"%s_count\">0</span>\n", e->id);
      buf_printf(&b, "  </div>\n");
    } else if (kid_is(e->tag, "INPUT")) {
      buf_printf(&b, "  <div class=\"ew-kid-row\">\n");
      buf_printf(&b, "    <input type=\"text\" id=\"%s\" class=\"ew-kid-input\" data-var=\"%s\" placeholder=\"%s\">\n",
                 e->id, e->var, e->placeholder ? html_escape(e->placeholder) : text);
      buf_printf(&b, "    <span class=\"ew-kid-value\" id=\"%s_value\"></span>\n", e->id);
      buf_printf(&b, "  </div>\n");
    } else if (kid_is(e->tag, "SLIDER")) {
      double mn = e->has_min ? e->min : 0;
      double mx = e->has_max ? e->max : 100;
      double vl = e->has_value ? e->value : mn;
      buf_printf(&b, "  <div class=\"ew-kid-row\">\n");
      buf_printf(&b, "    <input type=\"range\" id=\"%s\" class=\"ew-kid-slider\" data-var=\"%s\" min=\"%g\" max=\"%g\" value=\"%g\">\n",
                 e->id, e->var, mn, mx, vl);
      buf_printf(&b, "    <span class=\"ew-kid-value\" id=\"%s_value\">%g</span>\n", e->id, vl);
      buf_printf(&b, "  </div>\n");
    } else if (kid_is(e->tag, "IMAGE")) {
      buf_printf(&b, "  <div class=\"ew-kid-row\">\n");
      buf_printf(&b, "    <img id=\"%s\" class=\"ew-kid-image\" data-var=\"%s\" src=\"%s\" alt=\"%s\">\n",
                 e->id, e->var, e->src ? html_escape(e->src) : "", text);
      buf_printf(&b, "  </div>\n");
    } else if (kid_is(e->tag, "LINK")) {
      buf_printf(&b, "  <div class=\"ew-kid-row\">\n");
      buf_printf(&b, "    <a id=\"%s\" class=\"ew-kid-link\" data-var=\"%s\" href=\"%s\">%s</a>\n",
                 e->id, e->var, e->url ? html_escape(e->url) : "#", text);
      buf_printf(&b, "  </div>\n");
    } else {
      buf_printf(&b, "  <div id=\"%s\" class=\"ew-kid-box\" data-var=\"%s\">%s</div>\n", e->id, e->var, text);
    }
    free(text);
  }
  buf_puts(&b, "</div>\n");
  return buf_release(&b);
}

char* kid_render_js(KidBlock* kb) {
  Buf b;
  buf_init(&b);
  if (!kb) return buf_release(&b);
  for (int i = 0; i < kb->count; i++) {
    KidElem* e = &kb->items[i];
    if (kid_is(e->tag, "BUTTON")) {
      char* text = ew_asprintf("\"%s\"", json_escape(e->text, strlen(e->text)));
      buf_printf(&b, "EW.on('#%s','click',function(){\n", e->id);
      buf_printf(&b, "  var c=document.getElementById('%s_count'); if(c){c.textContent=(parseInt(c.textContent,10)||0)+1;}\n", e->id);
      buf_printf(&b, "  EW.toast(%s);\n", text);
      buf_printf(&b, "  EW.kidClick(%s,\"%s\");\n", text, json_escape(e->id, strlen(e->id)));
      buf_printf(&b, "});\n");
      free(text);
    } else if (kid_is(e->tag, "INPUT")) {
      buf_printf(&b, "EW.on('#%s','input',function(){ var v=document.getElementById('%s_value'); if(v){v.textContent=this.value;} });\n",
                 e->id, e->id);
    } else if (kid_is(e->tag, "SLIDER")) {
      buf_printf(&b, "EW.on('#%s','input',function(){ var v=document.getElementById('%s_value'); if(v){v.textContent=this.value;} });\n",
                 e->id, e->id);
    }
  }
  return buf_release(&b);
}
