/* easyweb - parser: tokens -> AST. */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

static Node* parse_stmt(Parser* p);
static Node* parse_expr(Parser* p);
static Node* parse_block(Parser* p);
static Node* parse_assign(Parser* p);
static Node* parse_postfix(Parser* p);
static Node* parse_primary(Parser* p);
static Node* parse_object(Parser* p);

/* ------------------------------------------------------------------ */
/* token plumbing                                                      */
/* ------------------------------------------------------------------ */

void parser_init(Parser* p, const char* src, size_t len, const char* file, bool kidmode) {
  memset(p, 0, sizeof(*p));
  /* skip a UTF-8 byte order mark, some editors add one */
  if (len >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) {
    src += 3;
    len -= 3;
  }
  lexer_init(&p->lx, src, len, file);
  diag_init(&p->diag);
  p->kidmode = kidmode;
  p->cur.type = TK_EOF;
  p->ahead.type = TK_EOF;
}

static void p_advance(Parser* p) {
  token_clear(&p->cur);
  if (p->has_ahead) {
    p->cur = p->ahead;
    p->has_ahead = false;
    memset(&p->ahead, 0, sizeof(p->ahead));
  } else {
    memset(&p->cur, 0, sizeof(p->cur));
    if (!lexer_next(&p->lx, &p->cur)) {
      if (!p->failed) {
        p->failed = true;
        p->diag = p->lx.diag;
      }
    }
  }
}

static Token* p_peek(Parser* p) {
  return &p->cur;
}

static Token* p_peek2(Parser* p) {
  if (!p->has_ahead) {
    memset(&p->ahead, 0, sizeof(p->ahead));
    if (!lexer_next(&p->lx, &p->ahead)) {
      /* lexer failed; ahead stays EOF */
    }
    p->has_ahead = true;
  }
  return &p->ahead;
}

static void p_error(Parser* p, const char* code, const char* fmt, ...) {
  if (p->failed) return;
  p->failed = true;
  char msg[400];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  diag_set(&p->diag, p->lx.file, p->cur.line, p->cur.col, code, "%s", msg);
}

static bool p_check(Parser* p, TokType t) {
  return p->cur.type == t;
}

static bool p_check_kw(Parser* p, Keyword k) {
  return p->cur.type == TK_KEYWORD && p->cur.kw == k;
}

static bool p_expect(Parser* p, TokType t, const char* what) {
  if (p->cur.type == t) {
    p_advance(p);
    return true;
  }
  p_error(p, "E110", "Expected %s but found '%s'", what, p->cur.text ? p->cur.text : token_desc(&p->cur));
  return false;
}

bool parser_had_error(const Parser* p) {
  return p->failed;
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static Node* make_bin(const char* op, Node* l, Node* r, int line, int col) {
  Node* n = node_new(N_BIN, line, col);
  n->op = ew_strdup(op);
  n->left = l;
  n->right = r;
  return n;
}

static Node* parse_primary(Parser* p) {
  Token* t = p_peek(p);
  switch (t->type) {
    case TK_INT:
    case TK_FLOAT: {
      Node* n = node_new(N_NUM, t->line, t->col);
      n->num = t->num;
      p_advance(p);
      return n;
    }
    case TK_STRING:
    case TK_TEMPLATE: {
      Node* n = node_new(N_STR, t->line, t->col);
      n->str = ew_strdup(t->strval ? t->strval : "");
      p_advance(p);
      return n;
    }
    case TK_KEYWORD: {
      if (t->kw >= KW_GET && t->kw <= KW_DELETE) {
        /* get(), post(), put(), delete() are also normal function names */
        Node* n = node_new(N_IDENT, t->line, t->col);
        n->str = ew_strdup(t->text);
        p_advance(p);
        return n;
      }
      if (t->kw == KW_TRUE || t->kw == KW_FALSE) {        Node* n = node_new(N_BOOL, t->line, t->col);
        n->bval = (t->kw == KW_TRUE);
        p_advance(p);
        return n;
      }
      if (t->kw == KW_NULL) {
        Node* n = node_new(N_NULL, t->line, t->col);
        p_advance(p);
        return n;
      }
      if (t->kw == KW_FUNC || t->kw == KW_CLIENT) {
        bool client = false;
        if (t->kw == KW_CLIENT) {
          client = true;
          p_advance(p);
          if (!p_check_kw(p, KW_FUNC)) {
            p_error(p, "E112", "Expected 'func' after 'client'");
            return NULL;
          }
        }
        p_advance(p);
        Node* f = node_new(N_FUNC, t->line, t->col);
        f->bval = client;
        if (p_check(p, TK_IDENT)) {
          f->name = ew_strdup(p->cur.text);
          p_advance(p);
        }
        if (!p_expect(p, TK_LPAREN, "(")) return NULL;
        while (!p_check(p, TK_RPAREN) && !p_check(p, TK_EOF) && !p->failed) {
          if (!p_check(p, TK_IDENT)) {
            p_error(p, "E112", "Parameter names must be names");
            return NULL;
          }
          node_push(f, node_new(N_IDENT, p->cur.line, p->cur.col));
          f->items[f->count - 1]->str = ew_strdup(p->cur.text);
          p_advance(p);
          if (p_check(p, TK_COMMA)) p_advance(p);
          else break;
        }
        if (!p_expect(p, TK_RPAREN, ")")) return NULL;
        f->body = parse_block(p);
        return f;
      }
      break;
    }
    case TK_IDENT: {
      Node* n = node_new(N_IDENT, t->line, t->col);
      n->str = ew_strdup(t->text);
      p_advance(p);
      return n;
    }
    case TK_LPAREN: {
      int line = t->line, col = t->col;
      p_advance(p);
      Node* e = parse_expr(p);
      if (p->failed) return NULL;
      if (!p_expect(p, TK_RPAREN, ")")) return NULL;
      if (e) e->paren = true;
      if (e) {
        e->line = line;
        e->col = col;
      }
      return e;
    }
    case TK_LBRACKET: {
      Node* n = node_new(N_ARRAY, t->line, t->col);
      p_advance(p);
      while (!p_check(p, TK_RBRACKET) && !p_check(p, TK_EOF) && !p->failed) {
        Node* e = parse_expr(p);
        if (p->failed) return NULL;
        node_push(n, e);
        if (p_check(p, TK_COMMA)) p_advance(p);
        else break;
      }
      if (!p_expect(p, TK_RBRACKET, "]")) return NULL;
      return n;
    }
    case TK_LBRACE:
      return parse_object(p);
    default:
      break;
  }
  p_error(p, "E113", "Unexpected '%s' - expected a value",
          p->cur.text ? p->cur.text : token_desc(&p->cur));
  return NULL;
}

static Node* parse_object(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_OBJECT, t->line, t->col);
  p_advance(p); /* { */
  while (!p_check(p, TK_RBRACE) && !p_check(p, TK_EOF) && !p->failed) {
    char* key = NULL;
    if (p_check(p, TK_IDENT) || p_check(p, TK_KEYWORD)) {
      key = ew_strdup(p->cur.text);
      p_advance(p);
    } else if (p_check(p, TK_STRING)) {
      key = ew_strdup(p->cur.strval ? p->cur.strval : "");
      p_advance(p);
    } else {
      p_error(p, "E114", "Expected a property name inside { }");
      return NULL;
    }
    if (!p_expect(p, TK_COLON, ":")) return NULL;
    Node* v = parse_expr(p);
    if (p->failed) return NULL;
    node_push_prop(n, key, v);
    free(key);
    /* properties can be separated by a comma or just by a new line */
    if (p_check(p, TK_COMMA)) p_advance(p);
  }
  if (!p_expect(p, TK_RBRACE, "}")) return NULL;
  return n;
}

static Node* parse_postfix(Parser* p) {
  Node* left = parse_primary(p);
  if (!left) return NULL;
  for (;;) {
    if (p_check(p, TK_LPAREN)) {
      Node* call = node_new(N_CALL, p->cur.line, p->cur.col);
      call->left = left;
      p_advance(p);
      while (!p_check(p, TK_RPAREN) && !p_check(p, TK_EOF) && !p->failed) {
        Node* a = parse_expr(p);
        if (p->failed) return NULL;
        node_push(call, a);
        if (p_check(p, TK_COMMA)) p_advance(p);
        else break;
      }
      if (!p_expect(p, TK_RPAREN, ")")) return NULL;
      left = call;
      continue;
    }
    if (p_check(p, TK_DOT)) {
      Node* m = node_new(N_MEMBER, p->cur.line, p->cur.col);
      m->left = left;
      p_advance(p);
      if (!p_check(p, TK_IDENT) && !p_check(p, TK_KEYWORD)) {
        p_error(p, "E115", "Expected a name after '.'");
        return NULL;
      }
      m->str = ew_strdup(p->cur.text);
      p_advance(p);
      left = m;
      continue;
    }
    if (p_check(p, TK_INC) || p_check(p, TK_DEC)) {
      bool inc = p->cur.type == TK_INC;
      if (left->kind != N_IDENT && left->kind != N_MEMBER && left->kind != N_INDEX) {
        p_error(p, "E130", "Only a variable can be increased or decreased");
        return NULL;
      }
      Node* a = node_new(N_ASSIGN, p->cur.line, p->cur.col);
      a->op = ew_strdup(inc ? "+=" : "-=");
      a->left = left;
      Node* one = node_new(N_NUM, p->cur.line, p->cur.col);
      one->num = 1;
      a->right = one;
      p_advance(p);
      left = a;
      continue;
    }
    if (p_check(p, TK_LBRACKET)) {
      Node* ix = node_new(N_INDEX, p->cur.line, p->cur.col);
      ix->left = left;
      p_advance(p);
      Node* idx = parse_expr(p);
      if (p->failed) return NULL;
      ix->right = idx;
      if (!p_expect(p, TK_RBRACKET, "]")) return NULL;
      left = ix;
      continue;
    }
    break;
  }
  return left;
}

static Node* parse_unary(Parser* p) {
  if (p_check(p, TK_INC) || p_check(p, TK_DEC)) {
    bool inc = p->cur.type == TK_INC;
    Token* t = p_peek(p);
    p_advance(p);
    Node* target = parse_unary(p);
    if (p->failed) return NULL;
    if (target->kind != N_IDENT && target->kind != N_MEMBER && target->kind != N_INDEX) {
      p_error(p, "E130", "Only a variable can be increased or decreased");
      return NULL;
    }
    Node* a = node_new(N_ASSIGN, t->line, t->col);
    a->op = ew_strdup(inc ? "+=" : "-=");
    a->left = target;
    Node* one = node_new(N_NUM, t->line, t->col);
    one->num = 1;
    a->right = one;
    return a;
  }
  if (p_check(p, TK_BANG) || p_check_kw(p, KW_NOT) || p_check(p, TK_MINUS)) {
    Token* t = p_peek(p);
    const char* op = (t->type == TK_BANG) ? "!" : (t->type == TK_MINUS ? "-" : "!");
    Node* n = node_new(N_UN, t->line, t->col);
    n->op = ew_strdup(op);
    p_advance(p);
    n->left = parse_unary(p);
    if (p->failed) return NULL;
    return n;
  }
  if (p_check_kw(p, KW_NEW)) {
    Token* t = p_peek(p);
    Node* n = node_new(N_CALL, t->line, t->col);
    n->name = ew_strdup("new");
    p_advance(p);
    Node* callee = parse_postfix(p);
    if (p->failed) return NULL;
    n->left = callee;
    return n;
  }
  return parse_postfix(p);
}

static Node* parse_mul(Parser* p) {
  Node* l = parse_unary(p);
  if (!l) return NULL;
  while (p_check(p, TK_STAR) || p_check(p, TK_SLASH) || p_check(p, TK_PERCENT)) {
    const char* op = p->cur.type == TK_STAR ? "*" : (p->cur.type == TK_SLASH ? "/" : "%");
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* r = parse_unary(p);
    if (p->failed) return NULL;
    l = make_bin(op, l, r, line, col);
  }
  return l;
}

static Node* parse_add(Parser* p) {
  Node* l = parse_mul(p);
  if (!l) return NULL;
  while (p_check(p, TK_PLUS) || p_check(p, TK_MINUS)) {
    const char* op = p->cur.type == TK_PLUS ? "+" : "-";
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* r = parse_mul(p);
    if (p->failed) return NULL;
    l = make_bin(op, l, r, line, col);
  }
  return l;
}

static Node* parse_rel(Parser* p) {
  Node* l = parse_add(p);
  if (!l) return NULL;
  while (p_check(p, TK_LT) || p_check(p, TK_LE) || p_check(p, TK_GT) || p_check(p, TK_GE)) {
    const char* op = p->cur.type == TK_LT ? "<" : (p->cur.type == TK_LE ? "<=" : (p->cur.type == TK_GT ? ">" : ">="));
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* r = parse_add(p);
    if (p->failed) return NULL;
    l = make_bin(op, l, r, line, col);
  }
  return l;
}

static Node* parse_eq(Parser* p) {
  Node* l = parse_rel(p);
  if (!l) return NULL;
  while (p_check(p, TK_EQ) || p_check(p, TK_NE)) {
    const char* op = p->cur.type == TK_EQ ? "==" : "!=";
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* r = parse_rel(p);
    if (p->failed) return NULL;
    l = make_bin(op, l, r, line, col);
  }
  return l;
}

static Node* parse_and(Parser* p) {
  Node* l = parse_eq(p);
  if (!l) return NULL;
  while (p_check(p, TK_AMP) || p_check_kw(p, KW_AND)) {
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* r = parse_eq(p);
    if (p->failed) return NULL;
    l = make_bin("&&", l, r, line, col);
  }
  return l;
}

static Node* parse_or(Parser* p) {
  Node* l = parse_and(p);
  if (!l) return NULL;
  while (p_check(p, TK_PIPE) || p_check_kw(p, KW_OR)) {
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* r = parse_and(p);
    if (p->failed) return NULL;
    l = make_bin("||", l, r, line, col);
  }
  return l;
}

static Node* parse_ternary(Parser* p) {
  Node* c = parse_or(p);
  if (!c) return NULL;
  if (p_check(p, TK_QUEST)) {
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* a = parse_expr(p);
    if (p->failed) return NULL;
    if (!p_expect(p, TK_COLON, ":")) return NULL;
    Node* b = parse_expr(p);
    if (p->failed) return NULL;
    Node* n = node_new(N_BIN, line, col);
    n->op = ew_strdup("?:");
    n->left = c;
    n->right = a;
    n->third = b;
    return n;
  }
  return c;
}

static Node* parse_arrow(Parser* p, Node* param) {
  /* current token is => */
  int line = p->cur.line, col = p->cur.col;
  p_advance(p);
  Node* f = node_new(N_FUNC, line, col);
  if (param) node_push(f, param);
  if (p_check(p, TK_LBRACE)) {
    f->body = parse_block(p);
  } else {
    Node* ret = node_new(N_RETURN, line, col);
    ret->left = parse_expr(p);
    f->body = node_new(N_BLOCK, line, col);
    node_push(f->body, ret);
  }
  if (p->failed) return NULL;
  return f;
}

static Node* parse_assign(Parser* p) {
  /* arrow with a single bare parameter:  x => x * 2 */
  if (p_check(p, TK_IDENT) && p_peek2(p)->type == TK_ARROW) {
    Node* param = node_new(N_IDENT, p->cur.line, p->cur.col);
    param->str = ew_strdup(p->cur.text);
    p_advance(p);
    return parse_arrow(p, param);
  }
  Node* left = parse_ternary(p);
  if (!left) return NULL;
  TokType t = p->cur.type;
  if (t == TK_ASSIGN || t == TK_PLUSEQ || t == TK_MINUSEQ || t == TK_STAREQ || t == TK_SLASHEQ) {
    const char* op = (t == TK_ASSIGN) ? "=" : (t == TK_PLUSEQ ? "+=" : (t == TK_MINUSEQ ? "-=" : (t == TK_STAREQ ? "*=" : "/=")));
    int line = p->cur.line, col = p->cur.col;
    p_advance(p);
    Node* right = parse_assign(p);
    if (p->failed) return NULL;
    if (left->kind != N_IDENT && left->kind != N_MEMBER && left->kind != N_INDEX) {
      p_error(p, "E116", "You can only assign to a name, a property or an item");
      return NULL;
    }
    Node* n = node_new(N_ASSIGN, line, col);
    n->op = ew_strdup(op);
    n->left = left;
    n->right = right;
    return n;
  }
  /* arrow with parenthesised params: (a, b) => a + b */
  if (left->kind == N_IDENT && left->paren && p_check(p, TK_ARROW)) {
    return parse_arrow(p, left);
  }
  return left;
}

static Node* parse_expr(Parser* p) {
  return parse_assign(p);
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static Node* parse_block(Parser* p) {
  Token* t = p_peek(p);
  Node* b = node_new(N_BLOCK, t->line, t->col);
  if (!p_expect(p, TK_LBRACE, "{")) return NULL;
  while (!p_check(p, TK_RBRACE) && !p_check(p, TK_EOF) && !p->failed) {
    Node* s = parse_stmt(p);
    if (p->failed) {
      node_free(b);
      return NULL;
    }
    if (s) node_push(b, s);
  }
  if (!p_expect(p, TK_RBRACE, "}")) {
    node_free(b);
    return NULL;
  }
  return b;
}

static Node* parse_var(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_VAR, t->line, t->col);
  n->op = ew_strdup(t->kw == KW_VAR ? "var" : (t->kw == KW_LET ? "let" : "const"));
  p_advance(p);
  if (!p_check(p, TK_IDENT)) {
    p_error(p, "E117", "Expected a variable name after %s", n->op);
    return NULL;
  }
  n->name = ew_strdup(p->cur.text);
  p_advance(p);
  if (p_check(p, TK_ASSIGN)) {
    p_advance(p);
    n->right = parse_expr(p);
    if (p->failed) return NULL;
  }
  return n;
}

static Node* parse_if(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_IF, t->line, t->col);
  p_advance(p);
  if (!p_expect(p, TK_LPAREN, "(")) return NULL;
  n->cond = parse_expr(p);
  if (p->failed) return NULL;
  if (!p_expect(p, TK_RPAREN, ")")) return NULL;
  n->body = parse_stmt(p);
  if (p->failed) return NULL;
  if (p_check_kw(p, KW_ELSE)) {
    p_advance(p);
    n->elseBody = parse_stmt(p);
    if (p->failed) return NULL;
  }
  return n;
}

static Node* parse_while(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_WHILE, t->line, t->col);
  p_advance(p);
  if (!p_expect(p, TK_LPAREN, "(")) return NULL;
  n->cond = parse_expr(p);
  if (p->failed) return NULL;
  if (!p_expect(p, TK_RPAREN, ")")) return NULL;
  n->body = parse_stmt(p);
  if (p->failed) return NULL;
  return n;
}

static Node* parse_for(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_FOR, t->line, t->col);
  p_advance(p);
  if (!p_expect(p, TK_LPAREN, "(")) return NULL;

  bool decl = false;
  const char* declkw = NULL;
  if (p_check_kw(p, KW_VAR) || p_check_kw(p, KW_LET) || p_check_kw(p, KW_CONST)) {
    decl = true;
    declkw = (p->cur.kw == KW_VAR) ? "var" : (p->cur.kw == KW_LET ? "let" : "const");
    p_advance(p);
  }

  /* for (x in list) or for (let x in list) */
  if (p_check(p, TK_IDENT) && p_peek2(p)->type == TK_KEYWORD && p_peek2(p)->kw == KW_IN) {
    n->kind = N_FORIN;
    n->name = ew_strdup(p->cur.text);
    p_advance(p);
    p_advance(p); /* in */
    n->right = parse_expr(p);
    if (p->failed) return NULL;
    if (!p_expect(p, TK_RPAREN, ")")) return NULL;
    n->body = parse_stmt(p);
    if (p->failed) return NULL;
    return n;
  }

  /* for (init; cond; inc) */
  if (decl) {
    Node* v = node_new(N_VAR, p->cur.line, p->cur.col);
    v->op = ew_strdup(declkw);
    if (!p_check(p, TK_IDENT)) {
      p_error(p, "E117", "Expected a variable name after %s", declkw);
      return NULL;
    }
    v->name = ew_strdup(p->cur.text);
    p_advance(p);
    if (p_check(p, TK_ASSIGN)) {
      p_advance(p);
      v->right = parse_expr(p);
      if (p->failed) return NULL;
    }
    n->init = v;
  } else if (!p_check(p, TK_SEMI)) {
    if (p_check_kw(p, KW_VAR) || p_check_kw(p, KW_LET) || p_check_kw(p, KW_CONST)) {
      n->init = parse_var(p);
    } else {
      n->init = node_new(N_EXPR, p->cur.line, p->cur.col);
      n->init->left = parse_expr(p);
    }
    if (p->failed) return NULL;
  }
  if (!p_expect(p, TK_SEMI, ";")) return NULL;
  if (!p_check(p, TK_SEMI)) {
    n->cond = parse_expr(p);
    if (p->failed) return NULL;
  }
  if (!p_expect(p, TK_SEMI, ";")) return NULL;
  if (!p_check(p, TK_RPAREN)) {
    n->inc = node_new(N_EXPR, p->cur.line, p->cur.col);
    n->inc->left = parse_expr(p);
    if (p->failed) return NULL;
  }
  if (!p_expect(p, TK_RPAREN, ")")) return NULL;
  n->body = parse_stmt(p);
  if (p->failed) return NULL;
  return n;
}

static Node* parse_route(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_ROUTE, t->line, t->col);
  p_advance(p);
  /* method */
  if (p_check(p, TK_KEYWORD) && p->cur.kw >= KW_GET && p->cur.kw <= KW_DELETE) {
    n->name = ew_strdup(keyword_name(p->cur.kw));
    p_advance(p);
  } else if (p_check(p, TK_IDENT)) {
    n->name = ew_strdup(p->cur.text);
    p_advance(p);
  } else {
    p_error(p, "E118", "Expected a method name (get, post, put, patch, delete)");
    return NULL;
  }
  for (char* q = n->name; *q; q++) *q = (char)((*q >= 'A' && *q <= 'Z') ? *q - 'A' + 'a' : *q);
  if (!p_check(p, TK_STRING)) {
    p_error(p, "E119", "Expected a path in quotes, like \"/api/hello\"");
    return NULL;
  }
  n->name2 = ew_strdup(p->cur.strval ? p->cur.strval : "/");
  p_advance(p);
  n->body = parse_block(p);
  if (p->failed) return NULL;
  return n;
}

static Node* parse_use(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_USE, t->line, t->col);
  p_advance(p);
  if (p_check(p, TK_STRING)) {
    n->name = ew_strdup(p->cur.strval ? p->cur.strval : "");
    p_advance(p);
  } else if (p_check(p, TK_IDENT) || p_check(p, TK_KEYWORD)) {
    n->name = ew_strdup(p->cur.text);
    p_advance(p);
  } else {
    p_error(p, "E120", "Expected a package name after use");
    return NULL;
  }
  return n;
}

static Node* parse_raw_stmt(Parser* p, NodeKind kind) {
  Token* t = p_peek(p);
  Node* n = node_new(kind, t->line, t->col);
  p_advance(p); /* keyword */
  /* the lexer produced the raw body next; skip an explicit '{' if present */
  if (p_check(p, TK_LBRACE)) p_advance(p);
  if (p_check(p, TK_RAW)) {
    n->str = ew_strdup(p->cur.strval ? p->cur.strval : "");
    p_advance(p);
  } else {
    p_error(p, "E121", "Expected a { } block here");
    return NULL;
  }
  if (!p_expect(p, TK_RBRACE, "}")) return NULL;
  return n;
}

static Node* parse_script(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_SCRIPT, t->line, t->col);
  p_advance(p);
  n->body = parse_block(p);
  if (p->failed) return NULL;
  return n;
}

static Node* parse_data(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_DATA, t->line, t->col);
  p_advance(p);
  n->left = parse_object(p);
  if (p->failed) return NULL;
  return n;
}

static Node* parse_try(Parser* p) {
  Token* t = p_peek(p);
  Node* n = node_new(N_TRY, t->line, t->col);
  p_advance(p);
  n->body = parse_block(p);
  if (p->failed) return NULL;
  if (p_check_kw(p, KW_CATCH)) {
    p_advance(p);
    if (p_check(p, TK_LPAREN)) {
      p_advance(p);
      if (p_check(p, TK_IDENT)) {
        n->name = ew_strdup(p->cur.text);
        p_advance(p);
      }
      if (!p_expect(p, TK_RPAREN, ")")) return NULL;
    } else if (p_check(p, TK_IDENT)) {
      n->name = ew_strdup(p->cur.text);
      p_advance(p);
    }
    n->elseBody = parse_block(p);
    if (p->failed) return NULL;
  }
  return n;
}

static Node* parse_stmt(Parser* p) {
  Token* t = p_peek(p);
  if (t->type == TK_SEMI) {
    p_advance(p);
    return NULL;
  }
  if (t->type == TK_KEYWORD) {
    switch (t->kw) {
      case KW_VAR:
      case KW_LET:
      case KW_CONST:
        return parse_var(p);
      case KW_IF:
        return parse_if(p);
      case KW_WHILE:
        return parse_while(p);
      case KW_FOR:
        return parse_for(p);
      case KW_FUNC:
        return parse_primary(p);
      case KW_CLIENT:
        return parse_primary(p);
      case KW_RETURN: {
        Node* n = node_new(N_RETURN, t->line, t->col);
        p_advance(p);
        if (!p_check(p, TK_SEMI) && !p_check(p, TK_RBRACE) && !p_check(p, TK_EOF)) {
          n->left = parse_expr(p);
          if (p->failed) return NULL;
        }
        return n;
      }
      case KW_BREAK: {
        Node* n = node_new(N_BREAK, t->line, t->col);
        p_advance(p);
        return n;
      }
      case KW_CONTINUE: {
        Node* n = node_new(N_CONTINUE, t->line, t->col);
        p_advance(p);
        return n;
      }
      case KW_PRINT: {
        Node* n = node_new(N_PRINT, t->line, t->col);
        p_advance(p);
        n->left = parse_expr(p);
        if (p->failed) return NULL;
        return n;
      }
      case KW_THROW: {
        Node* n = node_new(N_THROW, t->line, t->col);
        p_advance(p);
        n->left = parse_expr(p);
        if (p->failed) return NULL;
        return n;
      }
      case KW_ROUTE:
        return parse_route(p);
      case KW_USE:
        return parse_use(p);
      case KW_PAGE: {
        Node* n = node_new(N_PAGE, t->line, t->col);
        p_advance(p);
        if (p_check(p, TK_STRING)) {
          n->str = ew_strdup(p->cur.strval ? p->cur.strval : "");
          p_advance(p);
        } else {
          p_error(p, "E122", "Expected a page title in quotes");
          return NULL;
        }
        return n;
      }
      case KW_PORT: {
        Node* n = node_new(N_PORT, t->line, t->col);
        p_advance(p);
        if (p_check(p, TK_INT)) {
          n->num = p->cur.num;
          p_advance(p);
        } else {
          p_error(p, "E123", "Expected a port number");
          return NULL;
        }
        return n;
      }
      case KW_STYLE:
        return parse_raw_stmt(p, N_STYLE);
      case KW_UI:
        return parse_raw_stmt(p, N_UI);
      case KW_KID:
        return parse_raw_stmt(p, N_KID);
      case KW_KIDMODE: {
        Node* n = node_new(N_KIDMODE, t->line, t->col);
        p_advance(p);
        if (p_check(p, TK_IDENT)) {
          n->bval = (strcmp(p->cur.text, "on") == 0 || strcmp(p->cur.text, "true") == 0);
          p_advance(p);
        } else if (p_check(p, TK_KEYWORD) && (p->cur.kw == KW_TRUE || p->cur.kw == KW_FALSE)) {
          n->bval = (p->cur.kw == KW_TRUE);
          p_advance(p);
        } else {
          n->bval = true;
        }
        return n;
      }
      case KW_SCRIPT:
        return parse_script(p);
      case KW_DATA:
        return parse_data(p);
      case KW_TRY:
        return parse_try(p);
      case KW_TRUE:
      case KW_FALSE:
      case KW_NULL:
      case KW_NOT:
      case KW_NEW:
      case KW_GET:
      case KW_POST:
      case KW_PUT:
      case KW_PATCH:
      case KW_DELETE:
        break; /* fall through to expression statement */
      default:
        p_error(p, "E124", "'%s' cannot start a line here", keyword_name(t->kw));
        return NULL;
    }
  }
  if (p_check(p, TK_LBRACE)) {
    return parse_block(p);
  }
  /* expression statement */
  Node* e = parse_expr(p);
  if (p->failed) return NULL;
  Node* n = node_new(N_EXPR, e ? e->line : t->line, e ? e->col : t->col);
  n->left = e;
  return n;
}

/* ------------------------------------------------------------------ */
/* program                                                             */
/* ------------------------------------------------------------------ */

Node* parser_parse(Parser* p) {
  Node* prog = node_new(N_BLOCK, 1, 1);
  p_advance(p);
  while (!p_check(p, TK_EOF) && !p->failed) {
    Node* s = parse_stmt(p);
    if (p->failed) break;
    if (s) node_push(prog, s);
  }
  if (p->failed) {
    node_free(prog);
    return NULL;
  }
  return prog;
}

Node* parser_parse_expr(Parser* p, const char* src, size_t len, const char* file) {
  parser_init(p, src, len, file, false);
  p_advance(p);
  Node* e = parse_expr(p);
  if (p->failed) return NULL;
  return e;
}
