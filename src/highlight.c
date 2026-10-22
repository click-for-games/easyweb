/* easyweb - syntax highlighting for the terminal and for the web. */
#include "ew.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum { HL_CODE = 0, HL_UI, HL_STYLE, HL_KID } HlMode;

typedef enum {
  C_PLAIN = 0,
  C_KEYWORD,
  C_STRING,
  C_NUMBER,
  C_COMMENT,
  C_OPERATOR,
  C_BUILTIN,
  C_TAG,
  C_ATTR,
  C_KID,
  C_PROP,
  C_SELECTOR
} HlClass;

static const char* ansi_for(HlClass c) {
  switch (c) {
    case C_KEYWORD: return "\033[1;36m";
    case C_STRING: return "\033[32m";
    case C_NUMBER: return "\033[35m";
    case C_COMMENT: return "\033[90m";
    case C_OPERATOR: return "\033[33m";
    case C_BUILTIN: return "\033[34m";
    case C_TAG: return "\033[1;36m";
    case C_ATTR: return "\033[33m";
    case C_KID: return "\033[1;33m";
    case C_PROP: return "\033[34m";
    case C_SELECTOR: return "\033[1;35m";
    default: return "";
  }
}

static const char* html_class_for(HlClass c) {
  switch (c) {
    case C_KEYWORD: return "kw";
    case C_STRING: return "str";
    case C_NUMBER: return "num";
    case C_COMMENT: return "com";
    case C_OPERATOR: return "op";
    case C_BUILTIN: return "bi";
    case C_TAG: return "tag";
    case C_ATTR: return "attr";
    case C_KID: return "kid";
    case C_PROP: return "prop";
    case C_SELECTOR: return "sel";
    default: return "";
  }
}

static const char* KNOWN_BUILTINS[] = {"print",   "log",      "len",      "type",     "str",
                                       "num",     "int",      "bool",     "json",     "parse_json",
                                       "now",     "range",    "keys",     "values",   "push",
                                       "pop",     "upper",    "lower",    "trim",     "split",
                                       "join",    "abs",      "round",    "floor",    "ceil",
                                       "min",     "max",      "escape",   "status",   "header",
                                       "redirect", "html",    "send",     "read_file", "write_file",
                                       "file_exists", "app_name", "app_dir", "random", NULL};

static bool is_known_builtin(const char* s) {
  for (int i = 0; KNOWN_BUILTINS[i]; i++) {
    if (strcmp(KNOWN_BUILTINS[i], s) == 0) return true;
  }
  return false;
}

static const char* KID_WORDS[] = {"BUTTON", "DIV",         "INPUT", "SLIDER", "IMAGE",  "LINK",
                                  "TEXT",   "VAR",         "SRC",   "URL",    "MIN",    "MAX",
                                  "VALUE",  "PLACEHOLDER", "ACTION", "ID",    "CLASS", "TYPE", NULL};

static bool is_kid_word(const char* s) {
  for (int i = 0; KID_WORDS[i]; i++) {
    if (strcmp(KID_WORDS[i], s) == 0) return true;
  }
  return false;
}

typedef struct {
  Buf b;
  bool html;
} Emitter;

static void emit_text(Emitter* e, const char* text, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char ch = text[i];
    switch (ch) {
      case '&': buf_puts(&e->b, "&amp;"); break;
      case '<': buf_puts(&e->b, "&lt;"); break;
      case '>': buf_puts(&e->b, "&gt;"); break;
      default: buf_putc(&e->b, ch);
    }
  }
}

static void emit(Emitter* e, HlClass c, const char* text, size_t len) {
  if (len == 0) return;
  if (!e->html) {
    if (c == C_PLAIN) {
      buf_putn(&e->b, text, len);
      return;
    }
    buf_puts(&e->b, ansi_for(c));
    buf_putn(&e->b, text, len);
    buf_puts(&e->b, "\033[0m");
    return;
  }
  const char* cls = html_class_for(c);
  if (!cls || !*cls) {
    emit_text(e, text, len);
    return;
  }
  buf_printf(&e->b, "<span class=\"%s\">", cls);
  emit_text(e, text, len);
  buf_puts(&e->b, "</span>");
}

static bool ident_start(char c) {
  return isalpha((unsigned char)c) || c == '_' || c == '$';
}
static bool ident_char(char c) {
  return isalnum((unsigned char)c) || c == '_' || c == '$';
}

static char* highlight_run(const char* src, size_t len, bool html) {
  Emitter e;
  memset(&e, 0, sizeof(e));
  buf_init(&e.b);
  e.html = html;

  size_t i = 0;
  HlMode mode = HL_CODE;
  HlMode pending = HL_CODE;

  while (i < len) {
    char c = src[i];

    /* ---------------- comments ---------------- */
    if (mode == HL_CODE) {
      if (c == '/' && i + 1 < len && src[i + 1] == '/') {
        size_t s = i;
        while (i < len && src[i] != '\n') i++;
        emit(&e, C_COMMENT, src + s, i - s);
        continue;
      }
      if (c == '#') {
        size_t s = i;
        while (i < len && src[i] != '\n') i++;
        emit(&e, C_COMMENT, src + s, i - s);
        continue;
      }
      if (c == '/' && i + 1 < len && src[i + 1] == '*') {
        size_t s = i;
        i += 2;
        while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) i++;
        i = (i + 1 < len) ? i + 2 : len;
        emit(&e, C_COMMENT, src + s, i - s);
        continue;
      }
    }

    /* ---------------- strings ---------------- */
    if (mode != HL_STYLE && (c == '"' || c == '\'' || c == '`')) {
      char q = c;
      size_t s = i;
      i++;
      while (i < len) {
        if (src[i] == '\\' && i + 1 < len) {
          i += 2;
          continue;
        }
        if (src[i] == q) {
          i++;
          break;
        }
        if (src[i] == '\n' && q != '`') break;
        i++;
      }
      emit(&e, C_STRING, src + s, i - s);
      continue;
    }

    /* ---------------- kid values <like this> ---------------- */
    if (mode == HL_KID && c == '<') {
      size_t s = i;
      i++;
      while (i < len && src[i] != '>') i++;
      if (i < len) i++;
      emit(&e, C_STRING, src + s, i - s);
      continue;
    }

    /* ---------------- html tags ---------------- */
    if (mode == HL_UI && c == '<') {
      size_t s = i;
      i++;
      if (i < len && (src[i] == '/' || src[i] == '!')) i++;
      while (i < len && (ident_char(src[i]) || src[i] == '-')) i++;
      emit(&e, C_TAG, src + s, i - s);
      continue;
    }
    if (mode == HL_UI && c == '>') {
      emit(&e, C_TAG, src + i, 1);
      i++;
      continue;
    }
    if (mode == HL_UI && c == '}') {
      emit(&e, C_TAG, src + i, 1);
      i++;
      mode = HL_CODE;
      continue;
    }

    /* ---------------- numbers ---------------- */
    if (isdigit((unsigned char)c) && !(i > 0 && ident_char(src[i - 1]))) {
      size_t s = i;
      while (i < len && (isdigit((unsigned char)src[i]) || src[i] == '.')) i++;
      if (i < len && (src[i] == 'e' || src[i] == 'E')) {
        size_t save = i;
        i++;
        if (i < len && (src[i] == '+' || src[i] == '-')) i++;
        if (i < len && isdigit((unsigned char)src[i])) {
          while (i < len && isdigit((unsigned char)src[i])) i++;
        } else {
          i = save;
        }
      }
      emit(&e, C_NUMBER, src + s, i - s);
      continue;
    }

    /* ---------------- words ---------------- */
    if (ident_start(c)) {
      size_t s = i;
      while (i < len && ident_char(src[i])) i++;
      size_t wlen = i - s;
      char* word = ew_strndup(src + s, wlen);
      HlClass cls = C_PLAIN;

      if (mode == HL_KID) {
        if (is_kid_word(word)) cls = C_KID;
      } else if (mode == HL_STYLE) {
        if (i < len && src[i] == ':') cls = C_PROP;
        else if (src[s] == '.' || src[s] == '#') cls = C_SELECTOR;
        else if (i < len && src[i] == '{') cls = C_SELECTOR;
      } else if (mode == HL_UI) {
        if (i < len && src[i] == '=') cls = C_ATTR;
      } else {
        Keyword kw = keyword_lookup(word);
        if (kw != KW_NONE) {
          cls = C_KEYWORD;
          if (kw == KW_UI || kw == KW_STYLE || kw == KW_KID) {
            /* look ahead for the opening brace */
            size_t j = i;
            while (j < len && (src[j] == ' ' || src[j] == '\t' || src[j] == '\n' || src[j] == '\r')) j++;
            if (j < len && src[j] == '{') {
              pending = (kw == KW_UI) ? HL_UI : (kw == KW_STYLE ? HL_STYLE : HL_KID);
            }
          }
        } else if (is_known_builtin(word)) {
          cls = C_BUILTIN;
        }
      }
      emit(&e, cls, word, wlen);
      free(word);
      continue;
    }

    /* ---------------- punctuation ---------------- */
    if (strchr("{}()[];,.:+-*/%=<>!&|?", c)) {
      emit(&e, C_OPERATOR, src + i, 1);
      i++;
      if (c == '{') {
        if (pending != HL_CODE) {
          mode = pending;
          pending = HL_CODE;
        }
      } else if (c == '}' && mode != HL_CODE) {
        mode = HL_CODE;
      }
      continue;
    }

    emit(&e, C_PLAIN, src + i, 1);
    i++;
  }

  return buf_release(&e.b);
}

char* highlight_ansi(const char* src, size_t len) {
  return highlight_run(src, len, false);
}

char* highlight_html(const char* src, size_t len, bool full_document) {
  char* body = highlight_run(src, len, true);
  if (!full_document) return body;
  Buf b;
  buf_init(&b);
  buf_puts(&b, "<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
  buf_puts(&b, "<title>easyweb source</title><style>");
  buf_puts(&b,
           "body{background:#0f1117;color:#e8eaf0;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;"
           "padding:28px;font-size:14px;line-height:1.55;overflow-x:auto}");
  buf_puts(&b, ".kw{color:#4dd4ff;font-weight:600}.str{color:#7ee787}.num{color:#d2a8ff}");
  buf_puts(&b, ".com{color:#6b7280;font-style:italic}.op{color:#ffb86b}.bi{color:#79c0ff}");
  buf_puts(&b, ".tag{color:#4dd4ff}.attr{color:#ffb86b}.kid{color:#ffd866;font-weight:700}");
  buf_puts(&b, ".prop{color:#79c0ff}.sel{color:#d2a8ff}");
  buf_puts(&b, "</style></head><body><pre>");
  buf_puts(&b, body);
  buf_puts(&b, "</pre></body></html>");
  buf_free(&b);
  free(body);
  return buf_release(&b);
}
