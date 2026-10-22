/* easyweb - client side JavaScript generator.
 * The same AST that runs on the server is compiled to JS for the browser.
 */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* client runtime                                                      */
/* ------------------------------------------------------------------ */

const char* ew_client_runtime_js(void) {
  return
      "var EW=(function(){\n"
      "  function q(sel){return Array.prototype.slice.call(document.querySelectorAll(sel));}\n"
      "  function one(sel){return document.querySelector(sel);}\n"
      "  function err(e){console.error(e);}\n"
      "  var EW={\n"
      "    on:function(sel,ev,fn){q(sel).forEach(function(el){el.addEventListener(ev,fn);});return true;},\n"
      "    get:function(url,cb){return fetch(url,{headers:{'Accept':'application/json'}}).then(function(r){return r.json();}).then(function(d){if(cb)cb(d);return d;}).catch(function(e){err(e);if(cb)cb(null);return null;});},\n"
      "    post:function(url,body,cb){return EW.send('POST',url,body,cb);},\n"
      "    put:function(url,body,cb){return EW.send('PUT',url,body,cb);},\n"
      "    del:function(url,body,cb){return EW.send('DELETE',url,body,cb);},\n"
      "    send:function(method,url,body,cb){return fetch(url,{method:method,headers:{'Content-Type':'application/json','Accept':'application/json'},body:body===undefined?undefined:JSON.stringify(body)}).then(function(r){return r.json();}).then(function(d){if(cb)cb(d);return d;}).catch(function(e){err(e);if(cb)cb(null);return null;});},\n"
      "    fetch:function(url,opts){return fetch(url,opts);},\n"
      "    text:function(sel,v){q(sel).forEach(function(el){el.textContent=(v===undefined?'':v);});},\n"
      "    html:function(sel,v){q(sel).forEach(function(el){el.innerHTML=(v===undefined?'':v);});},\n"
      "    val:function(sel,v){if(v===undefined){var el=one(sel);return el?el.value:'';}q(sel).forEach(function(el){el.value=v;});return true;},\n"
      "    addClass:function(sel,c){q(sel).forEach(function(el){el.classList.add(c);});},\n"
      "    removeClass:function(sel,c){q(sel).forEach(function(el){el.classList.remove(c);});},\n"
      "    toggleClass:function(sel,c){q(sel).forEach(function(el){el.classList.toggle(c);});},\n"
      "    show:function(sel){q(sel).forEach(function(el){el.style.display='';});},\n"
      "    hide:function(sel){q(sel).forEach(function(el){el.style.display='none';});},\n"
      "    toast:function(msg){var t=document.createElement('div');t.className='ew-toast';t.textContent=msg;document.body.appendChild(t);setTimeout(function(){t.classList.add('ew-toast-out');setTimeout(function(){if(t.parentNode)t.parentNode.removeChild(t);},300);},1800);},\n"
      "    kidClick:function(text,id){try{fetch('/__kid/click',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({text:text,id:id})});}catch(e){}},\n"
      "    json:function(v){return JSON.stringify(v);},\n"
      "    parse:function(s){try{return JSON.parse(s);}catch(e){return null;}},\n"
      "    len:function(v){if(v===null||v===undefined)return 0;if(typeof v==='string'||Array.isArray(v))return v.length;if(typeof v==='object')return Object.keys(v).length;return 0;},\n"
      "    str:function(v){return v===null||v===undefined?'':String(v);},\n"
      "    num:function(v){var n=parseFloat(v);return isNaN(n)?0:n;},\n"
      "    type:function(v){if(v===null||v===undefined)return 'null';if(Array.isArray(v))return 'array';return typeof v;},\n"
      "    log:function(){console.log.apply(console,arguments);},\n"
      "    alert:function(m){window.alert(m);}\n"
      "  };\n"
      "  return EW;\n"
      "})();\n";
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static const char* CLIENT_BUILTINS[] = {"on",     "get",       "post",     "put",     "del",
                                        "text",   "html",      "val",      "addClass", "removeClass",
                                        "toggleClass", "show", "hide",   "toast",   "log",
                                        "alert",  "fetch",     "send",     "json",    "parse",
                                        "len",    "str",       "num",      "type",    NULL};

static bool is_client_builtin(const char* name) {
  for (int i = 0; CLIENT_BUILTINS[i]; i++) {
    if (strcmp(CLIENT_BUILTINS[i], name) == 0) return true;
  }
  return false;
}

static char* js_string(const char* s) {
  char* e = json_escape(s ? s : "", s ? strlen(s) : 0);
  char* out = ew_asprintf("\"%s\"", e);
  free(e);
  return out;
}

static char* js_number(double d) {
  if (d == (double)(long long)d && d < 1e15 && d > -1e15) return ew_asprintf("%lld", (long long)d);
  return ew_asprintf("%.17g", d);
}

static void emit_stmt(Buf* b, Node* n, int indent);

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static char* js_expr(Node* n) {
  if (!n) return ew_strdup("undefined");
  switch (n->kind) {
    case N_NUM:
      return js_number(n->num);
    case N_STR:
      return js_string(n->str);
    case N_BOOL:
      return ew_strdup(n->bval ? "true" : "false");
    case N_NULL:
      return ew_strdup("null");
    case N_IDENT:
      return ew_strdup(n->str);
    case N_BIN: {
      if (strcmp(n->op, "?:") == 0) {
        char* c = js_expr(n->left);
        char* a = js_expr(n->right);
        char* b = js_expr(n->third);
        char* out = ew_asprintf("(%s ? %s : %s)", c, a, b);
        free(c); free(a); free(b);
        return out;
      }
      char* l = js_expr(n->left);
      char* r = js_expr(n->right);
      char* out = ew_asprintf("(%s %s %s)", l, n->op, r);
      free(l); free(r);
      return out;
    }
    case N_UN: {
      char* l = js_expr(n->left);
      char* out = ew_asprintf("(%s%s)", n->op, l);
      free(l);
      return out;
    }
    case N_ASSIGN: {
      char* l = js_expr(n->left);
      char* r = js_expr(n->right);
      char* out = ew_asprintf("(%s %s %s)", l, n->op, r);
      free(l); free(r);
      return out;
    }
    case N_CALL: {
      Buf b;
      buf_init(&b);
      Node* callee = n->left;
      if (callee->kind == N_IDENT && is_client_builtin(callee->str)) {
        buf_printf(&b, "EW.%s(", callee->str);
      } else {
        char* c = js_expr(callee);
        buf_printf(&b, "%s(", c);
        free(c);
      }
      for (int i = 0; i < n->count; i++) {
        if (i) buf_puts(&b, ", ");
        char* a = js_expr(n->items[i]);
        buf_puts(&b, a);
        free(a);
      }
      buf_puts(&b, ")");
      return buf_release(&b);
    }
    case N_MEMBER: {
      char* l = js_expr(n->left);
      char* out = ew_asprintf("%s.%s", l, n->str);
      free(l);
      return out;
    }
    case N_INDEX: {
      char* l = js_expr(n->left);
      char* r = js_expr(n->right);
      char* out = ew_asprintf("%s[%s]", l, r);
      free(l); free(r);
      return out;
    }
    case N_ARRAY: {
      Buf b;
      buf_init(&b);
      buf_putc(&b, '[');
      for (int i = 0; i < n->count; i++) {
        if (i) buf_puts(&b, ", ");
        char* a = js_expr(n->items[i]);
        buf_puts(&b, a);
        free(a);
      }
      buf_putc(&b, ']');
      return buf_release(&b);
    }
    case N_OBJECT: {
      Buf b;
      buf_init(&b);
      buf_putc(&b, '{');
      for (int i = 0; i < n->propcount; i++) {
        if (i) buf_puts(&b, ", ");
        char* k = js_string(n->props[i].key);
        buf_printf(&b, "%s: ", k);
        free(k);
        char* v = js_expr(n->props[i].value);
        buf_puts(&b, v);
        free(v);
      }
      buf_putc(&b, '}');
      return buf_release(&b);
    }
    case N_FUNC: {
      Buf b;
      buf_init(&b);
      buf_puts(&b, "function ");
      if (n->name) buf_printf(&b, "%s", n->name);
      buf_putc(&b, '(');
      for (int i = 0; i < n->count; i++) {
        if (i) buf_puts(&b, ", ");
        buf_printf(&b, "%s", n->items[i]->str);
      }
      buf_puts(&b, ") {\n");
      if (n->body) {
        for (int i = 0; i < n->body->count; i++) {
          emit_stmt(&b, n->body->items[i], 1);
        }
      }
      buf_puts(&b, "}");
      return buf_release(&b);
    }
    default:
      break;
  }
  return ew_strdup("undefined");
}

char* jsgen_expr(Node* n) {
  return js_expr(n);
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static void indent_str(Buf* b, int indent) {
  for (int i = 0; i < indent; i++) buf_puts(b, "  ");
}

static void emit_stmt(Buf* b, Node* n, int indent) {
  if (!n) return;
  switch (n->kind) {
    case N_VAR: {
      indent_str(b, indent);
      buf_printf(b, "%s %s", n->op && strcmp(n->op, "var") == 0 ? "var" : "let", n->name);
      if (n->right) {
        char* e = js_expr(n->right);
        buf_printf(b, " = %s", e);
        free(e);
      }
      buf_puts(b, ";\n");
      break;
    }
    case N_EXPR: {
      indent_str(b, indent);
      char* e = js_expr(n->left);
      buf_printf(b, "%s;\n", e);
      free(e);
      break;
    }
    case N_PRINT: {
      indent_str(b, indent);
      char* e = js_expr(n->left);
      buf_printf(b, "console.log(%s);\n", e);
      free(e);
      break;
    }
    case N_IF: {
      indent_str(b, indent);
      char* c = js_expr(n->cond);
      buf_printf(b, "if (%s) {\n", c);
      free(c);
      if (n->body) {
        if (n->body->kind == N_BLOCK) {
          for (int i = 0; i < n->body->count; i++) emit_stmt(b, n->body->items[i], indent + 1);
        } else {
          emit_stmt(b, n->body, indent + 1);
        }
      }
      indent_str(b, indent);
      buf_puts(b, "}");
      if (n->elseBody) {
        buf_puts(b, " else {\n");
        if (n->elseBody->kind == N_BLOCK) {
          for (int i = 0; i < n->elseBody->count; i++) emit_stmt(b, n->elseBody->items[i], indent + 1);
        } else {
          emit_stmt(b, n->elseBody, indent + 1);
        }
        indent_str(b, indent);
        buf_puts(b, "}");
      }
      buf_puts(b, "\n");
      break;
    }
    case N_WHILE: {
      indent_str(b, indent);
      char* c = js_expr(n->cond);
      buf_printf(b, "while (%s) {\n", c);
      free(c);
      if (n->body) {
        if (n->body->kind == N_BLOCK) {
          for (int i = 0; i < n->body->count; i++) emit_stmt(b, n->body->items[i], indent + 1);
        } else {
          emit_stmt(b, n->body, indent + 1);
        }
      }
      indent_str(b, indent);
      buf_puts(b, "}\n");
      break;
    }
    case N_FOR: {
      indent_str(b, indent);
      buf_puts(b, "for (");
      if (n->init) {
        if (n->init->kind == N_VAR) {
          char* e = n->init->right ? js_expr(n->init->right) : ew_strdup("undefined");
          buf_printf(b, "%s %s = %s", n->init->op && strcmp(n->init->op, "var") == 0 ? "var" : "let",
                     n->init->name, e);
          free(e);
        } else {
          char* e = js_expr(n->init->left);
          buf_printf(b, "%s", e);
          free(e);
        }
      }
      buf_puts(b, "; ");
      if (n->cond) {
        char* e = js_expr(n->cond);
        buf_printf(b, "%s", e);
        free(e);
      }
      buf_puts(b, "; ");
      if (n->inc) {
        char* e = js_expr(n->inc->left);
        buf_printf(b, "%s", e);
        free(e);
      }
      buf_puts(b, ") {\n");
      if (n->body) {
        if (n->body->kind == N_BLOCK) {
          for (int i = 0; i < n->body->count; i++) emit_stmt(b, n->body->items[i], indent + 1);
        } else {
          emit_stmt(b, n->body, indent + 1);
        }
      }
      indent_str(b, indent);
      buf_puts(b, "}\n");
      break;
    }
    case N_FORIN: {
      indent_str(b, indent);
      char* c = js_expr(n->right);
      buf_printf(b, "for (let %s of %s) {\n", n->name, c);
      free(c);
      if (n->body) {
        if (n->body->kind == N_BLOCK) {
          for (int i = 0; i < n->body->count; i++) emit_stmt(b, n->body->items[i], indent + 1);
        } else {
          emit_stmt(b, n->body, indent + 1);
        }
      }
      indent_str(b, indent);
      buf_puts(b, "}\n");
      break;
    }
    case N_RETURN: {
      indent_str(b, indent);
      if (n->left) {
        char* e = js_expr(n->left);
        buf_printf(b, "return %s;\n", e);
        free(e);
      } else {
        buf_puts(b, "return;\n");
      }
      break;
    }
    case N_BREAK:
      indent_str(b, indent);
      buf_puts(b, "break;\n");
      break;
    case N_CONTINUE:
      indent_str(b, indent);
      buf_puts(b, "continue;\n");
      break;
    case N_THROW: {
      indent_str(b, indent);
      char* e = js_expr(n->left);
      buf_printf(b, "throw %s;\n", e);
      free(e);
      break;
    }
    case N_TRY: {
      indent_str(b, indent);
      buf_puts(b, "try {\n");
      if (n->body) {
        for (int i = 0; i < n->body->count; i++) emit_stmt(b, n->body->items[i], indent + 1);
      }
      indent_str(b, indent);
      buf_puts(b, "} catch (e) {\n");
      if (n->elseBody) {
        for (int i = 0; i < n->elseBody->count; i++) emit_stmt(b, n->elseBody->items[i], indent + 1);
      }
      indent_str(b, indent);
      buf_puts(b, "}\n");
      break;
    }
    case N_BLOCK: {
      indent_str(b, indent);
      buf_puts(b, "{\n");
      for (int i = 0; i < n->count; i++) emit_stmt(b, n->items[i], indent + 1);
      indent_str(b, indent);
      buf_puts(b, "}\n");
      break;
    }
    case N_FUNC: {
      indent_str(b, indent);
      char* f = js_expr(n);
      buf_printf(b, "%s;\n", f);
      free(f);
      break;
    }
    default: {
      indent_str(b, indent);
      char* e = js_expr(n);
      buf_printf(b, "%s;\n", e);
      free(e);
      break;
    }
  }
}

char* jsgen_func(Node* func) {
  Buf b;
  buf_init(&b);
  emit_stmt(&b, func, 0);
  return buf_release(&b);
}
