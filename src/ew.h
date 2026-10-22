/* easyweb - a tiny, fast, secure web programming language.
 * Core declarations.
 */
#ifndef EW_H
#define EW_H

/* ask for the POSIX and BSD parts of the system headers we need,
   even when the compiler runs in strict ISO C mode */
#if !defined(_WIN32)
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EW_VERSION "1.0.0"
#define EW_DEFAULT_PORT 8080
#define EW_MAX_INCLUDE_DEPTH 32

/* ------------------------------------------------------------------ */
/* utilities                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
  char* data;
  size_t len;
  size_t cap;
} Buf;

void buf_init(Buf* b);
void buf_free(Buf* b);
void buf_reserve(Buf* b, size_t extra);
void buf_putc(Buf* b, char c);
void buf_puts(Buf* b, const char* s);
void buf_putn(Buf* b, const char* s, size_t n);
void buf_printf(Buf* b, const char* fmt, ...);
char* buf_release(Buf* b); /* caller frees */

void* ew_malloc(size_t n);
void* ew_calloc(size_t n, size_t sz);
void* ew_realloc(void* p, size_t n);
char* ew_strdup(const char* s);
char* ew_strndup(const char* s, size_t n);
char* ew_asprintf(const char* fmt, ...);

/* path helpers (all return malloc'd strings) */
char* path_join(const char* a, const char* b);
char* path_dirname(const char* p);
char* path_basename(const char* p);
char* path_ext(const char* p);
char* path_strip_ext(const char* p);
bool path_exists(const char* p);
bool path_is_dir(const char* p);
bool path_mkdir_p(const char* p);
bool path_read_file(const char* p, char** out, size_t* out_len);
bool path_write_file(const char* p, const char* data, size_t len);
bool path_remove(const char* p);
char* path_cwd(void);
char* path_home(void);
char* path_abs(const char* p);

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
  DIAG_ERROR = 0,
  DIAG_WARN = 1,
  DIAG_INFO = 2,
  DIAG_FRIENDLY = 3 /* kid mode */
} DiagKind;

typedef struct {
  int line;   /* 1-based, 0 = unknown */
  int col;    /* 1-based, 0 = unknown */
  const char* file;
  char message[512];
  char hint[256];
  char code[16];
} Diag;

void diag_init(Diag* d);
void diag_free(Diag* d);
void diag_set(Diag* d, const char* file, int line, int col, const char* code, const char* fmt, ...);
void diag_hint(Diag* d, const char* fmt, ...);

/* prints a caret-annotated diagnostic to stderr */
void diag_print(const Diag* d, bool color);
/* kid friendly variant */
void diag_print_friendly(const Diag* d, bool color);
void diag_emit_ansi(bool on);
bool diag_ansi_enabled(void);
/* turn terminal colours on or off for the whole program */
void ew_print_ansi(bool on);

/* ------------------------------------------------------------------ */
/* lexer                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
  TK_EOF = 0,
  TK_IDENT,
  TK_KEYWORD,
  TK_INT,
  TK_FLOAT,
  TK_STRING,
  TK_TEMPLATE, /* backtick raw string */
  TK_RAW,     /* raw block: ui/style/kid bodies */
  TK_LBRACE,
  TK_RBRACE,
  TK_LPAREN,
  TK_RPAREN,
  TK_LBRACKET,
  TK_RBRACKET,
  TK_COMMA,
  TK_DOT,
  TK_COLON,
  TK_SEMI,
  TK_ARROW,
  TK_QUEST,
  TK_EQ,
  TK_NE,
  TK_LT,
  TK_LE,
  TK_GT,
  TK_GE,
  TK_PLUS,
  TK_MINUS,
  TK_STAR,
  TK_SLASH,
  TK_PERCENT,
  TK_AMP,
  TK_PIPE,
  TK_BANG,
  TK_ASSIGN,
  TK_PLUSEQ,
  TK_MINUSEQ,
  TK_STAREQ,
  TK_SLASHEQ,
  TK_PIPEEQ,
  TK_INC,
  TK_DEC
} TokType;

typedef enum {
  KW_NONE = 0,
  KW_PAGE,
  KW_PORT,
  KW_USE,
  KW_STYLE,
  KW_SCRIPT,
  KW_ROUTE,
  KW_GET,
  KW_POST,
  KW_PUT,
  KW_PATCH,
  KW_DELETE,
  KW_FUNC,
  KW_RETURN,
  KW_IF,
  KW_ELSE,
  KW_WHILE,
  KW_FOR,
  KW_IN,
  KW_VAR,
  KW_LET,
  KW_CONST,
  KW_TRUE,
  KW_FALSE,
  KW_NULL,
  KW_BREAK,
  KW_CONTINUE,
  KW_PRINT,
  KW_DATA,
  KW_UI,
  KW_KID,
  KW_KIDMODE,
  KW_AND,
  KW_OR,
  KW_NOT,
  KW_TRY,
  KW_CATCH,
  KW_THROW,
  KW_NEW,
  KW_CLIENT
} Keyword;

typedef struct {
  TokType type;
  Keyword kw;
  char* text;   /* raw source text for the token */
  char* strval; /* decoded value for strings */
  double num;
  int line;
  int col;
  size_t off; /* byte offset of token start */
} Token;

typedef struct {
  const char* src;
  size_t len;
  size_t pos;
  int line;
  int col;
  const char* file;
  bool failed;
  bool pending_raw;
  Diag diag;
} Lexer;

void lexer_init(Lexer* lx, const char* src, size_t len, const char* file);
bool lexer_next(Lexer* lx, Token* tok);
void token_clear(Token* t);
const char* token_desc(const Token* t);
const char* keyword_name(Keyword k);
Keyword keyword_lookup(const char* text);

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
  N_NUM,
  N_STR,
  N_BOOL,
  N_NULL,
  N_IDENT,
  N_BIN,
  N_UN,
  N_ASSIGN,
  N_CALL,
  N_INDEX,
  N_MEMBER,
  N_ARRAY,
  N_OBJECT,
  N_FUNC,
  N_BLOCK,
  N_VAR,
  N_IF,
  N_WHILE,
  N_FOR,
  N_FORIN,
  N_RETURN,
  N_BREAK,
  N_CONTINUE,
  N_EXPR,
  N_PRINT,
  N_ROUTE,
  N_USE,
  N_PAGE,
  N_PORT,
  N_STYLE,
  N_SCRIPT,
  N_DATA,
  N_UI,
  N_KID,
  N_KIDMODE,
  N_TRY,
  N_THROW,
  N_EMPTY
} NodeKind;

typedef struct Node Node;

typedef struct {
  char* key;
  Node* value;
} Prop;

struct Node {
  NodeKind kind;
  int line;
  int col;

  double num;
  char* str;      /* string literal / identifier name / operator */
  bool bval;
  char* op;       /* binary/unary/assignment operator */

  Node* left;
  Node* right;
  Node* third; /* else-branch / for-in collection */

  Node** items;
  int count;
  int cap;

  Prop* props;
  int propcount;
  int propcap;

  Node* body;
  Node* init;
  Node* cond;
  Node* inc;
  Node* elseBody;

  char* name;  /* variable name / func name / route method */
  char* name2; /* secondary name (route path, package name) */

  int line2;
  int col2;
  bool paren; /* expression was wrapped in parentheses */
};

Node* node_new(NodeKind kind, int line, int col);
void node_free(Node* n);
void node_push(Node* n, Node* child);
void node_push_prop(Node* n, const char* key, Node* value);
Node* node_clone(Node* n);

/* ------------------------------------------------------------------ */
/* parser                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
  Lexer lx;
  Token cur;
  Token ahead;
  bool has_ahead;
  bool failed;
  Diag diag;
  bool kidmode;
} Parser;

void parser_init(Parser* p, const char* src, size_t len, const char* file, bool kidmode);
Node* parser_parse(Parser* p);
Node* parser_parse_expr(Parser* p, const char* src, size_t len, const char* file);
bool parser_had_error(const Parser* p);

/* ------------------------------------------------------------------ */
/* values                                                              */
/* ------------------------------------------------------------------ */

typedef struct Value Value;
typedef struct Obj Obj;
typedef struct Arr Arr;
typedef struct Str Str;
typedef struct Func Func;
typedef struct Env Env;
typedef struct Interp Interp;

/* Native (C) implementation of a builtin or package function. */
typedef void (*NativeFn)(Interp* in, Value* args, int nargs, Value* out);

typedef enum { V_NULL = 0, V_BOOL, V_NUM, V_STR, V_ARR, V_OBJ, V_FUN } VKind;

struct Str {
  int refs;
  size_t len;
  char* data;
};

struct Arr {
  int refs;
  Value* items;
  size_t len;
  size_t cap;
};

struct Value {
  VKind k;
  union {
    bool b;
    double n;
    Str* s;
    Arr* a;
    Obj* o;
    Func* f;
  } as;
};

typedef struct {
  Value key;
  Value val;
} Entry;

struct Obj {
  int refs;
  Entry* entries;
  size_t count;
  size_t cap;
};

struct Func {
  int refs;
  Node* decl;
  Env* env;
  char* name;
  NativeFn native;
};
Value v_null(void);
Value v_bool(bool b);
Value v_num(double n);
Value v_str(const char* s);
Value v_strn(const char* s, size_t n);
Value v_str_take(Str* s);
Value v_arr(void);
Value v_obj(void);
Value v_fun(Node* decl, Env* env, const char* name);
Value v_native(const char* name, NativeFn fn);

void value_retain(Value* v);
void value_release(Value* v);
void value_free(Value* v);

Value arr_new(void);
void arr_push(Value* arr, Value v);
size_t arr_len(Value* arr);
Value arr_get(Value* arr, size_t i);
void arr_set(Value* arr, size_t i, Value v);
void arr_remove_at(Value* arr, size_t i);

Value obj_new(void);
bool obj_get(Value* obj, const char* key, Value* out);
void obj_set(Value* obj, const char* key, Value v);
bool obj_del(Value* obj, const char* key);
size_t obj_count(Value* obj);
char** obj_keys(Value* obj); /* malloc'd array of malloc'd strings */

bool value_truthy(Value v);
bool value_equals(Value a, Value b);
int value_compare(Value a, Value b);
char* value_to_string(Value v);
Value value_deep_copy(Value v);
Value value_index(Value obj, Value key);
void value_set_index(Value obj, Value key, Value v);

/* ------------------------------------------------------------------ */
/* environment                                                         */
/* ------------------------------------------------------------------ */

struct Env {
  Env* parent;
  Value vars;
};

Env* env_new(Env* parent);
void env_free(Env* e);
bool env_get(Env* e, const char* name, Value* out);
void env_set(Env* e, const char* name, Value v);
bool env_define(Env* e, const char* name, Value v);

/* ------------------------------------------------------------------ */
/* interpreter                                                         */
/* ------------------------------------------------------------------ */

typedef struct Interp Interp;

typedef struct {
  char* method;
  char* path;
  Node* handler;
  char** params; /* path params, e.g. :id */
  int nparams;
} Route;

typedef struct {
  Route* routes;
  int count;
  int cap;
} RouteTable;

struct Interp {
  Env* globals;
  RouteTable routes;
  Node* root;
  Buf css;
  Buf client_js;
  Buf pkg_func_js;
  char* page_title;
  char* kidmode;
  bool failed;
  Diag diag;
  void* db; /* dataweb handle, if loaded */
  int port;
  bool verbose;

  /* per-request response context (requests are serialised) */
  int resp_status;
  char* resp_content_type;
  Buf resp_headers;
  Buf resp_body;
  char* resp_location;
  bool resp_body_set;

  /* program parts */
  char* ui_raw;
  char* kid_raw;
  Node** script_blocks;
  int nscript;
  int script_cap;
  Node** client_funcs;
  int nclient_funcs;
  int client_funcs_cap;
  /* package programs are kept alive - their functions point into the tree */
  Node** pkg_progs;
  int npkg_progs;
  int pkg_progs_cap;
  bool kid;
  bool data_defined;
  char* app_path;
  char* app_dir;
  char* app_name;

  /* control flow */
  Value retval;
  bool has_ret;
  int depth;
};

Interp* interp_new(bool kidmode);
void interp_free(Interp* in);
bool interp_load_file(Interp* in, const char* path, Diag* out);
/* execute a route handler, result goes to *result */
bool interp_call_route(Interp* in, const Route* rt, Env* env, Value* result);
/* evaluate a single expression in the global env (used for templates) */
bool interp_eval(Interp* in, Env* env, Node* n, Value* out);
void interp_register_builtins(Interp* in);

/* ------------------------------------------------------------------ */
/* template / page rendering                                            */
/* ------------------------------------------------------------------ */

typedef struct {
  char* title;
  char* style;
  char* ui_raw;
  char* kid_html;
  char* client_js;
  char* extra_css;
} PageParts;

char* render_template(const char* raw, Interp* in, Env* env, bool* ok);
char* render_page(Interp* in, Env* env);

/* ------------------------------------------------------------------ */
/* kid mode                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
  char* tag;   /* BUTTON, DIV, INPUT ... */
  char* var;   /* variable name (auto if _) */
  char* text;  /* label */
  char* id;    /* generated id */
  char* action;
  char* placeholder;
  char* src;
  char* url;
  double min, max, value;
  bool has_min, has_max, has_value;
  int line;
} KidElem;

typedef struct {
  KidElem* items;
  int count;
  int cap;
} KidBlock;

KidBlock* kid_parse(const char* raw, const char* file, Diag* d);
void kid_free(KidBlock* kb);
char* kid_render_html(KidBlock* kb, bool kidmode);
char* kid_render_js(KidBlock* kb);

/* ------------------------------------------------------------------ */
/* JavaScript code generation (client side)                             */
/* ------------------------------------------------------------------ */

char* jsgen_program(Interp* in);
char* jsgen_func(Node* func);
char* jsgen_expr(Node* n);
const char* ew_client_runtime_js(void);

/* ------------------------------------------------------------------ */
/* JSON                                                                */
/* ------------------------------------------------------------------ */

Value json_parse(const char* text, size_t len, char* err, size_t errsz);
char* json_from_value(Value v);
char* json_escape(const char* s, size_t len);

/* ------------------------------------------------------------------ */
/* HTTP                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
  char* method;
  char* path;      /* decoded path */
  char* raw_path;  /* with query */
  char* query;
  char* body;
  size_t body_len;
  char** header_names;
  char** header_values;
  int nheaders;
  char** param_names;
  char** param_values;
  int nparams;
} HttpRequest;

typedef struct {
  int status;
  char* content_type;
  char* body;
  size_t body_len;
  char** header_names;
  char** header_values;
  int nheaders;
} HttpResponse;

void http_request_free(HttpRequest* r);
void http_response_free(HttpResponse* r);
void http_response_set_header(HttpResponse* r, const char* name, const char* value);
void http_response_send(HttpResponse* r, int status, const char* content_type, const char* body, size_t len);
char* http_response_render(HttpResponse* r, size_t* out_len);
bool http_parse_request(const char* raw, size_t len, HttpRequest* out, char* err, size_t errsz);

/* ------------------------------------------------------------------ */
/* server                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
  char* host;
  int port;
  Interp* interp;
  char* app_path;
  bool open_browser;
  bool kidmode;
  int max_threads;
} ServerOpts;

int server_run(ServerOpts* opts);

/* ------------------------------------------------------------------ */
/* packages                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
  const char* name;
  const char* version;
  const char* description;
  const char* entry_file; /* file name inside the package, e.g. animateeasy.eweb */
  const char* css_file;
  const char* js_file;
  const char* entry_code; /* the easyweb source of the package */
  const char* css_code;
  const char* js_code;
} EmbeddedPkg;

typedef struct {
  char* name;
  char* version;
  char* description;
  char* entry;   /* easyweb source file inside package */
  char* css;     /* css file inside package */
  char* js;      /* js file inside package */
  char* dir;     /* installed directory */
  bool builtin;
  bool installed;
} Package;

typedef struct {
  Package* items;
  int count;
  int cap;
} PackageList;

PackageList* pkg_builtin_registry(void);
PackageList* pkg_installed(const char* project_dir);
Package* pkg_find(PackageList* l, const char* name);
PackageList* pkg_search(PackageList* l, const char* query);
bool pkg_install(const char* name, const char* project_dir, bool global, char* err, size_t errsz);
bool pkg_remove(const char* name, const char* project_dir, bool global, char* err, size_t errsz);
char* pkg_manifest_json(const char* dir);
char* pkg_dir_project(const char* project_dir);
char* pkg_dir_global(void);
/* load a package's runtime source (malloc'd) */
char* pkg_load_entry(Package* p, size_t* len);
char* pkg_load_asset(Package* p, const char* which, size_t* len);
void pkg_list_free(PackageList* l);

/* embedded package payloads */
const EmbeddedPkg* embedded_packages(int* count);

/* ------------------------------------------------------------------ */
/* syntax highlighting                                                 */
/* ------------------------------------------------------------------ */

char* highlight_ansi(const char* src, size_t len);
char* highlight_html(const char* src, size_t len, bool full_document);

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

int cmd_run(int argc, char** argv);
int cmd_build(int argc, char** argv);
int cmd_check(int argc, char** argv);
int cmd_highlight(int argc, char** argv);
int cmd_new(int argc, char** argv);
int cmd_packages(int argc, char** argv);
int cmd_version(int argc, char** argv);
int cmd_help(int argc, char** argv);

void ew_print_usage(void);

/* webhelp */
int webhelp_main(int argc, char** argv);

#endif /* EW_H */
