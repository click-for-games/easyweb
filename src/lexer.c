/* easyweb - tokenizer. */
#include "ew.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const struct {
  const char* text;
  Keyword kw;
} KEYWORDS[] = {
    {"page", KW_PAGE},       {"port", KW_PORT},       {"use", KW_USE},
    {"style", KW_STYLE},     {"script", KW_SCRIPT},   {"route", KW_ROUTE},
    {"get", KW_GET},         {"post", KW_POST},       {"put", KW_PUT},
    {"patch", KW_PATCH},     {"delete", KW_DELETE},   {"func", KW_FUNC},
    {"function", KW_FUNC},   {"return", KW_RETURN},   {"if", KW_IF},
    {"else", KW_ELSE},       {"while", KW_WHILE},     {"for", KW_FOR},
    {"in", KW_IN},           {"var", KW_VAR},         {"let", KW_LET},
    {"const", KW_CONST},     {"true", KW_TRUE},       {"false", KW_FALSE},
    {"null", KW_NULL},       {"nil", KW_NULL},        {"break", KW_BREAK},
    {"continue", KW_CONTINUE}, {"print", KW_PRINT},   {"data", KW_DATA},
    {"ui", KW_UI},           {"kid", KW_KID},         {"kidmode", KW_KIDMODE},
    {"and", KW_AND},         {"or", KW_OR},           {"not", KW_NOT},
    {"try", KW_TRY},         {"catch", KW_CATCH},     {"throw", KW_THROW},
    {"new", KW_NEW},        {"client", KW_CLIENT},
    {NULL, KW_NONE}};

Keyword keyword_lookup(const char* text) {
  if (!text) return KW_NONE;
  for (int i = 0; KEYWORDS[i].text; i++) {
    if (strcmp(KEYWORDS[i].text, text) == 0) return KEYWORDS[i].kw;
  }
  return KW_NONE;
}

const char* keyword_name(Keyword k) {
  for (int i = 0; KEYWORDS[i].text; i++) {
    if (KEYWORDS[i].kw == k) return KEYWORDS[i].text;
  }
  return "?";
}

void lexer_init(Lexer* lx, const char* src, size_t len, const char* file) {
  memset(lx, 0, sizeof(*lx));
  lx->src = src;
  lx->len = len;
  lx->pos = 0;
  lx->line = 1;
  lx->col = 1;
  lx->file = file;
  diag_init(&lx->diag);
}

static int lx_peek(Lexer* lx, size_t off) {
  size_t p = lx->pos + off;
  if (p >= lx->len) return -1;
  return (unsigned char)lx->src[p];
}

static int lx_adv(Lexer* lx) {
  if (lx->pos >= lx->len) return -1;
  int c = (unsigned char)lx->src[lx->pos++];
  if (c == '\n') {
    lx->line++;
    lx->col = 1;
  } else {
    lx->col++;
  }
  return c;
}

static void tok_init(Token* t) {
  memset(t, 0, sizeof(*t));
}

static void tok_clear(Token* t) {
  free(t->text);
  free(t->strval);
  tok_init(t);
}

void token_clear(Token* t) {
  tok_clear(t);
}

static void set_text(Token* t, Lexer* lx, size_t start) {
  t->text = ew_strndup(lx->src + start, lx->pos - start);
}

static void fail(Lexer* lx, int line, int col, const char* code, const char* msg, const char* hint) {
  if (lx->failed) return;
  lx->failed = true;
  diag_set(&lx->diag, lx->file, line, col, code, "%s", msg);
  if (hint) diag_hint(&lx->diag, "%s", hint);
}

static void skip_ws(Lexer* lx) {
  for (;;) {
    int c = lx_peek(lx, 0);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      lx_adv(lx);
      continue;
    }
    if (c == '/' && lx_peek(lx, 1) == '/') {
      while (lx_peek(lx, 0) != -1 && lx_peek(lx, 0) != '\n') lx_adv(lx);
      continue;
    }
    if (c == '#') {
      while (lx_peek(lx, 0) != -1 && lx_peek(lx, 0) != '\n') lx_adv(lx);
      continue;
    }
    if (c == '/' && lx_peek(lx, 1) == '*') {
      int sl = lx->line, sc = lx->col;
      lx_adv(lx);
      lx_adv(lx);
      for (;;) {
        int d = lx_adv(lx);
        if (d == -1) {
          fail(lx, sl, sc, "E101", "Block comment is never closed", "Add */ at the end of the comment.");
          return;
        }
        if (d == '*' && lx_peek(lx, 0) == '/') {
          lx_adv(lx);
          break;
        }
      }
      continue;
    }
    break;
  }
}

/* Reads a raw block body starting after the opening brace.
 * Leaves the closing brace in the stream for the parser. */
static char* read_raw(Lexer* lx, size_t* out_len) {
  Buf b;
  buf_init(&b);
  int depth = 1;
  for (;;) {
    int c = lx_peek(lx, 0);
    if (c == -1) {
      free(buf_release(&b));
      fail(lx, lx->line, lx->col, "E102", "Block is never closed", "Add a matching } at the end of the block.");
      *out_len = 0;
      return NULL;
    }
    if (c == '"' || c == '\'') {
      int q = c;
      buf_putc(&b, (char)lx_adv(lx));
      while (lx_peek(lx, 0) != -1) {
        int d = lx_peek(lx, 0);
        if (d == q) {
          buf_putc(&b, (char)lx_adv(lx));
          break;
        }
        buf_putc(&b, (char)lx_adv(lx));
      }
      continue;
    }
    if (c == '/' && lx_peek(lx, 1) == '*') {
      buf_putc(&b, (char)lx_adv(lx));
      buf_putc(&b, (char)lx_adv(lx));
      while (lx_peek(lx, 0) != -1) {
        int d = lx_adv(lx);
        buf_putc(&b, (char)d);
        if (d == '*' && lx_peek(lx, 0) == '/') {
          buf_putc(&b, (char)lx_adv(lx));
          break;
        }
      }
      continue;
    }
    if (c == '{') {
      depth++;
      buf_putc(&b, (char)lx_adv(lx));
      continue;
    }
    if (c == '}') {
      depth--;
      if (depth == 0) break;
      buf_putc(&b, (char)lx_adv(lx));
      continue;
    }
    buf_putc(&b, (char)lx_adv(lx));
  }
  size_t n = b.len;
  char* s = buf_release(&b);
  *out_len = n;
  return s;
}

static char* decode_string(const char* src, size_t len) {
  Buf b;
  buf_init(&b);
  for (size_t i = 0; i < len; i++) {
    char c = src[i];
    if (c != '\\') {
      buf_putc(&b, c);
      continue;
    }
    if (i + 1 >= len) {
      buf_putc(&b, '\\');
      break;
    }
    char e = src[++i];
    switch (e) {
      case 'n': buf_putc(&b, '\n'); break;
      case 't': buf_putc(&b, '\t'); break;
      case 'r': buf_putc(&b, '\r'); break;
      case '0': buf_putc(&b, '\0'); break;
      case 'b': buf_putc(&b, '\b'); break;
      case 'f': buf_putc(&b, '\f'); break;
      case '\\': buf_putc(&b, '\\'); break;
      case '"': buf_putc(&b, '"'); break;
      case '\'': buf_putc(&b, '\''); break;
      case '\n': break;
      default:
        buf_putc(&b, '\\');
        buf_putc(&b, e);
        break;
    }
  }
  return buf_release(&b);
}

bool lexer_next(Lexer* lx, Token* tok) {
  tok_init(tok);
  skip_ws(lx);
  if (lx->failed) {
    tok->type = TK_EOF;
    tok->line = lx->line;
    tok->col = lx->col;
    return false;
  }
  size_t start = lx->pos;
  int line = lx->line, col = lx->col;
  int c = lx_peek(lx, 0);

  if (c == -1) {
    tok->type = TK_EOF;
    tok->line = line;
    tok->col = col;
    tok->off = start;
    return true;
  }

  /* raw blocks: ui { ... } / style { ... } / kid { ... } */
  if (lx->pending_raw) {
    lx->pending_raw = false;
    if (lx_peek(lx, 0) != '{') {
      fail(lx, lx->line, lx->col, "E106", "Expected { after this keyword",
           "Blocks look like: ui { ... }");
      tok->type = TK_EOF;
      return false;
    }
    lx_adv(lx);
    size_t rlen = 0;
    char* raw = read_raw(lx, &rlen);
    if (!raw) {
      tok->type = TK_EOF;
      return false;
    }
    tok->type = TK_RAW;
    tok->strval = raw;
    set_text(tok, lx, start);
    tok->line = line;
    tok->col = col;
    tok->off = start;
    return true;
  }

  if (isalpha(c) || c == '_' || c == '$') {
    while (isalnum(lx_peek(lx, 0)) || lx_peek(lx, 0) == '_' || lx_peek(lx, 0) == '$') lx_adv(lx);
    set_text(tok, lx, start);
    Keyword kw = keyword_lookup(tok->text);
    /* "data" only starts a data block when a { follows it, so you can still
       use data as a normal variable name (data.visits = 1). */
    if (kw == KW_DATA) {
      int off = 0;
      int nx = lx_peek(lx, off);
      while (nx == ' ' || nx == '\t' || nx == '\r' || nx == '\n') {
        off++;
        nx = lx_peek(lx, off);
      }
      if (nx != '{') kw = KW_NONE;
    }
    tok->type = (kw == KW_NONE) ? TK_IDENT : TK_KEYWORD;
    tok->kw = kw;
    tok->line = line;
    tok->col = col;
    tok->off = start;
    if (kw == KW_UI || kw == KW_STYLE || kw == KW_KID) lx->pending_raw = true;
    return true;
  }

  if (isdigit(c)) {
    while (isdigit(lx_peek(lx, 0))) lx_adv(lx);
    bool isfloat = false;
    if (lx_peek(lx, 0) == '.' && isdigit(lx_peek(lx, 1))) {
      isfloat = true;
      lx_adv(lx);
      while (isdigit(lx_peek(lx, 0))) lx_adv(lx);
    }
    if (lx_peek(lx, 0) == 'e' || lx_peek(lx, 0) == 'E') {
      size_t save = lx->pos;
      int sl = lx->line, sc = lx->col;
      lx_adv(lx);
      if (lx_peek(lx, 0) == '+' || lx_peek(lx, 0) == '-') lx_adv(lx);
      if (isdigit(lx_peek(lx, 0))) {
        isfloat = true;
        while (isdigit(lx_peek(lx, 0))) lx_adv(lx);
      } else {
        lx->pos = save;
        lx->line = sl;
        lx->col = sc;
      }
    }
    set_text(tok, lx, start);
    tok->type = isfloat ? TK_FLOAT : TK_INT;
    tok->num = atof(tok->text);
    tok->line = line;
    tok->col = col;
    tok->off = start;
    return true;
  }

  if (c == '"' || c == '\'') {
    int q = c;
    lx_adv(lx);
    Buf b;
    buf_init(&b);
    for (;;) {
      int d = lx_peek(lx, 0);
      if (d == -1 || d == '\n') {
        buf_free(&b);
        fail(lx, line, col, "E103", "String is never closed", "Add a matching quote at the end of the string.");
        tok->type = TK_EOF;
        tok->line = line;
        tok->col = col;
        return false;
      }
      if (d == q) {
        lx_adv(lx);
        break;
      }
      if (d == '\\' && lx_peek(lx, 1) == q) {
        buf_putc(&b, '\\');
        lx_adv(lx);
        buf_putc(&b, (char)lx_adv(lx));
        continue;
      }
      buf_putc(&b, (char)lx_adv(lx));
    }
    char* raw = buf_release(&b);
    tok->type = TK_STRING;
    tok->strval = decode_string(raw, strlen(raw));
    free(raw);
    set_text(tok, lx, start);
    tok->line = line;
    tok->col = col;
    tok->off = start;
    return true;
  }

  if (c == '`') {
    lx_adv(lx);
    Buf b;
    buf_init(&b);
    for (;;) {
      int d = lx_peek(lx, 0);
      if (d == -1) {
        buf_free(&b);
        fail(lx, line, col, "E104", "Backtick string is never closed", "Add a matching ` at the end.");
        tok->type = TK_EOF;
        return false;
      }
      if (d == '`') {
        lx_adv(lx);
        break;
      }
      if (d == '\\' && lx_peek(lx, 1) == '`') {
        lx_adv(lx);
        buf_putc(&b, (char)lx_adv(lx));
        continue;
      }
      buf_putc(&b, (char)lx_adv(lx));
    }
    tok->type = TK_TEMPLATE;
    tok->strval = buf_release(&b);
    set_text(tok, lx, start);
    tok->line = line;
    tok->col = col;
    tok->off = start;
    return true;
  }

  /* operators */
  int c1 = lx_peek(lx, 1);
  lx_adv(lx);
  TokType t;
  switch (c) {
    case '{': t = TK_LBRACE; break;
    case '}': t = TK_RBRACE; break;
    case '(': t = TK_LPAREN; break;
    case ')': t = TK_RPAREN; break;
    case '[': t = TK_LBRACKET; break;
    case ']': t = TK_RBRACKET; break;
    case ',': t = TK_COMMA; break;
    case '.': t = TK_DOT; break;
    case ':': t = TK_COLON; break;
    case ';': t = TK_SEMI; break;
    case '?': t = TK_QUEST; break;
    case '+':
      if (c1 == '=') { lx_adv(lx); t = TK_PLUSEQ; } else if (c1 == '+') { lx_adv(lx); t = TK_INC; } else t = TK_PLUS;
      break;
    case '-':
      if (c1 == '=') { lx_adv(lx); t = TK_MINUSEQ; }
      else if (c1 == '>') { lx_adv(lx); t = TK_ARROW; }
      else if (c1 == '-') { lx_adv(lx); t = TK_DEC; }
      else t = TK_MINUS;
      break;
    case '*':
      if (c1 == '=') { lx_adv(lx); t = TK_STAREQ; } else t = TK_STAR;
      break;
    case '/':
      if (c1 == '=') { lx_adv(lx); t = TK_SLASHEQ; } else t = TK_SLASH;
      break;
    case '%': t = TK_PERCENT; break;
    case '=':
      if (c1 == '=') { lx_adv(lx); t = TK_EQ; } else if (c1 == '>') { lx_adv(lx); t = TK_ARROW; } else t = TK_ASSIGN;
      break;
    case '!':
      if (c1 == '=') { lx_adv(lx); t = TK_NE; } else t = TK_BANG;
      break;
    case '<':
      if (c1 == '=') { lx_adv(lx); t = TK_LE; } else t = TK_LT;
      break;
    case '>':
      if (c1 == '=') { lx_adv(lx); t = TK_GE; } else t = TK_GT;
      break;
    case '&':
      if (c1 == '&') { lx_adv(lx); t = TK_AMP; } else t = TK_AMP;
      break;
    case '|':
      if (c1 == '|') { lx_adv(lx); t = TK_PIPE; } else if (c1 == '=') { lx_adv(lx); t = TK_PIPEEQ; } else t = TK_PIPE;
      break;
    default:
      lx->pos = start;
      lx->line = line;
      lx->col = col;
      fail(lx, line, col, "E105", "Unexpected character in source", "Remove the character or check the easyweb guide.");
      tok->type = TK_EOF;
      tok->line = line;
      tok->col = col;
      return false;
  }
  tok->type = t;
  set_text(tok, lx, start);
  tok->line = line;
  tok->col = col;
  tok->off = start;
  return true;
}

const char* token_desc(const Token* t) {
  switch (t->type) {
    case TK_EOF: return "end of file";
    case TK_IDENT: return "a name";
    case TK_KEYWORD: return keyword_name(t->kw);
    case TK_INT:
    case TK_FLOAT: return "a number";
    case TK_STRING:
    case TK_TEMPLATE: return "a string";
    case TK_RAW: return "a block";
    case TK_LBRACE: return "{";
    case TK_RBRACE: return "}";
    case TK_LPAREN: return "(";
    case TK_RPAREN: return ")";
    case TK_LBRACKET: return "[";
    case TK_RBRACKET: return "]";
    case TK_COMMA: return ",";
    case TK_DOT: return ".";
    case TK_COLON: return ":";
    case TK_SEMI: return ";";
    case TK_ARROW: return "=>";
    case TK_QUEST: return "?";
    case TK_EQ: return "==";
    case TK_NE: return "!=";
    case TK_LT: return "<";
    case TK_LE: return "<=";
    case TK_GT: return ">";
    case TK_GE: return ">=";
    case TK_PLUS: return "+";
    case TK_MINUS: return "-";
    case TK_STAR: return "*";
    case TK_SLASH: return "/";
    case TK_PERCENT: return "%";
    case TK_AMP: return "&&";
    case TK_PIPE: return "||";
    case TK_BANG: return "!";
    case TK_ASSIGN: return "=";
    case TK_PLUSEQ: return "+=";
    case TK_MINUSEQ: return "-=";
    case TK_STAREQ: return "*=";
    case TK_SLASHEQ: return "/=";
    case TK_PIPEEQ: return "|=";
    case TK_INC: return "++";
    case TK_DEC: return "--";
  }
  return "something";
}
