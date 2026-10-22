/* easyweb - runtime values, arrays, objects, environments. */
#include "ew.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* constructors                                                        */
/* ------------------------------------------------------------------ */

Value v_null(void) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_NULL;
  return v;
}

Value v_bool(bool b) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_BOOL;
  v.as.b = b;
  return v;
}

Value v_num(double n) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_NUM;
  v.as.n = n;
  return v;
}

Value v_strn(const char* s, size_t n) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_STR;
  v.as.s = (Str*)ew_calloc(1, sizeof(Str));
  v.as.s->refs = 1;
  v.as.s->len = n;
  v.as.s->data = (char*)ew_malloc(n + 1);
  if (n && s) memcpy(v.as.s->data, s, n);
  v.as.s->data[n] = 0;
  return v;
}

Value v_str(const char* s) {
  return v_strn(s, s ? strlen(s) : 0);
}

Value v_str_take(Str* s) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_STR;
  v.as.s = s;
  return v;
}

Value arr_new(void) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_ARR;
  v.as.a = (Arr*)ew_calloc(1, sizeof(Arr));
  v.as.a->refs = 1;
  return v;
}

Value v_obj(void) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_OBJ;
  v.as.o = (Obj*)ew_calloc(1, sizeof(Obj));
  v.as.o->refs = 1;
  return v;
}

Value v_fun(Node* decl, Env* env, const char* name) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_FUN;
  v.as.f = (Func*)ew_calloc(1, sizeof(Func));
  v.as.f->refs = 1;
  v.as.f->decl = decl;
  v.as.f->env = env;
  v.as.f->name = name ? ew_strdup(name) : NULL;
  v.as.f->native = NULL;
  return v;
}

Value v_native(const char* name, NativeFn fn) {
  Value v;
  memset(&v, 0, sizeof(v));
  v.k = V_FUN;
  v.as.f = (Func*)ew_calloc(1, sizeof(Func));
  v.as.f->refs = 1;
  v.as.f->native = fn;
  v.as.f->name = name ? ew_strdup(name) : NULL;
  return v;
}

/* ------------------------------------------------------------------ */
/* ref counting                                                        */
/* ------------------------------------------------------------------ */

void value_retain(Value* v) {
  if (!v) return;
  switch (v->k) {
    case V_STR: v->as.s->refs++; break;
    case V_ARR: v->as.a->refs++; break;
    case V_OBJ: v->as.o->refs++; break;
    case V_FUN: v->as.f->refs++; break;
    default: break;
  }
}

void value_release(Value* v) {
  if (!v) return;
  switch (v->k) {
    case V_STR:
      if (--v->as.s->refs == 0) {
        free(v->as.s->data);
        free(v->as.s);
      }
      break;
    case V_ARR: {
      Arr* a = v->as.a;
      if (--a->refs == 0) {
        for (size_t i = 0; i < a->len; i++) value_release(&a->items[i]);
        free(a->items);
        free(a);
      }
      break;
    }
    case V_OBJ: {
      Obj* o = v->as.o;
      if (--o->refs == 0) {
        for (size_t i = 0; i < o->count; i++) {
          value_release(&o->entries[i].key);
          value_release(&o->entries[i].val);
        }
        free(o->entries);
        free(o);
      }
      break;
    }
    case V_FUN: {
      Func* f = v->as.f;
      if (--f->refs == 0) {
        free(f->name);
        free(f);
      }
      break;
    }
    default: break;
  }
}

void value_free(Value* v) {
  value_release(v);
  memset(v, 0, sizeof(*v));
}

/* ------------------------------------------------------------------ */
/* arrays                                                              */
/* ------------------------------------------------------------------ */

size_t arr_len(Value* arr) {
  return (arr && arr->k == V_ARR) ? arr->as.a->len : 0;
}

void arr_push(Value* arr, Value v) {
  if (!arr || arr->k != V_ARR) return;
  Arr* a = arr->as.a;
  if (a->len == a->cap) {
    a->cap = a->cap ? a->cap * 2 : 8;
    a->items = (Value*)ew_realloc(a->items, sizeof(Value) * a->cap);
  }
  value_retain(&v);
  a->items[a->len++] = v;
}

Value arr_get(Value* arr, size_t i) {
  if (!arr || arr->k != V_ARR) return v_null();
  if (i >= arr->as.a->len) return v_null();
  return arr->as.a->items[i];
}

void arr_set(Value* arr, size_t i, Value v) {
  if (!arr || arr->k != V_ARR) return;
  Arr* a = arr->as.a;
  if (i >= a->len) {
    while (a->len <= i) arr_push(arr, v_null());
    arr_set(arr, i, v);
    return;
  }
  value_retain(&v);
  value_release(&a->items[i]);
  a->items[i] = v;
}

void arr_remove_at(Value* arr, size_t i) {
  if (!arr || arr->k != V_ARR) return;
  Arr* a = arr->as.a;
  if (i >= a->len) return;
  value_release(&a->items[i]);
  for (size_t j = i; j + 1 < a->len; j++) a->items[j] = a->items[j + 1];
  a->len--;
}

/* ------------------------------------------------------------------ */
/* objects                                                             */
/* ------------------------------------------------------------------ */

Value obj_new(void) {
  Obj* o = (Obj*)ew_calloc(1, sizeof(Obj));
  o->refs = 1;
  Value v;
  v.k = V_OBJ;
  v.as.o = o;
  return v;
}

static size_t str_hash(const char* s) {
  size_t h = 1469598103934665603ULL;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 1099511628211ULL;
  }
  return h;
}

static bool entry_matches(Entry* e, const char* key) {
  return e->key.k == V_STR && e->key.as.s && strcmp(e->key.as.s->data, key) == 0;
}

static void obj_grow(Obj* o) {
  size_t ncap = o->cap ? o->cap * 2 : 16;
  Entry* ne = (Entry*)ew_calloc(ncap, sizeof(Entry));
  size_t n = 0;
  for (size_t i = 0; i < o->cap; i++) {
    Entry* e = &o->entries[i];
    if (e->key.k == V_NULL) continue;
    size_t idx = str_hash(e->key.as.s->data) % ncap;
    while (ne[idx].key.k != V_NULL) idx = (idx + 1) % ncap;
    ne[idx] = *e;
    n++;
  }
  free(o->entries);
  o->entries = ne;
  o->cap = ncap;
  o->count = n;
}

bool obj_get(Value* obj, const char* key, Value* out) {
  if (!obj || obj->k != V_OBJ || !key) return false;
  Obj* o = obj->as.o;
  if (!o->cap) return false;
  size_t idx = str_hash(key) % o->cap;
  for (size_t probe = 0; probe < o->cap; probe++) {
    Entry* e = &o->entries[idx];
    if (e->key.k == V_NULL) return false;
    if (entry_matches(e, key)) {
      if (out) {
        *out = e->val;
        value_retain(out);
      }
      return true;
    }
    idx = (idx + 1) % o->cap;
  }
  return false;
}

void obj_set(Value* obj, const char* key, Value v) {
  if (!obj || obj->k != V_OBJ || !key) return;
  Obj* o = obj->as.o;
  if (!o->cap) obj_grow(o);
  size_t idx = str_hash(key) % o->cap;
  for (size_t probe = 0; probe < o->cap; probe++) {
    Entry* e = &o->entries[idx];
    if (e->key.k == V_NULL) {
      e->key = v_str(key);
      value_retain(&v);
      e->val = v;
      o->count++;
      if (o->count * 2 >= o->cap) obj_grow(o);
      return;
    }
    if (entry_matches(e, key)) {
      value_retain(&v);
      value_release(&e->val);
      e->val = v;
      return;
    }
    idx = (idx + 1) % o->cap;
  }
  obj_grow(o);
  obj_set(obj, key, v);
}

bool obj_del(Value* obj, const char* key) {
  if (!obj || obj->k != V_OBJ || !key) return false;
  Obj* o = obj->as.o;
  if (!o->cap) return false;
  size_t idx = str_hash(key) % o->cap;
  for (size_t probe = 0; probe < o->cap; probe++) {
    Entry* e = &o->entries[idx];
    if (e->key.k == V_NULL) return false;
    if (entry_matches(e, key)) {
      value_release(&e->key);
      value_release(&e->val);
      o->count--;
      /* backward shift deletion keeps the probe chains intact */
      size_t hole = idx;
      size_t j = (hole + 1) % o->cap;
      while (o->entries[j].key.k != V_NULL) {
        size_t home = str_hash(o->entries[j].key.as.s->data) % o->cap;
        if (((j - home) % o->cap) >= ((j - hole) % o->cap)) {
          o->entries[hole] = o->entries[j];
          hole = j;
        }
        j = (j + 1) % o->cap;
      }
      memset(&o->entries[hole], 0, sizeof(Entry));
      return true;
    }
    idx = (idx + 1) % o->cap;
  }
  return false;
}

size_t obj_count(Value* obj) {
  return (obj && obj->k == V_OBJ) ? obj->as.o->count : 0;
}

char** obj_keys(Value* obj) {
  size_t n = obj_count(obj);
  char** keys = (char**)ew_calloc(n + 1, sizeof(char*));
  if (!obj || obj->k != V_OBJ) return keys;
  size_t j = 0;
  for (size_t i = 0; i < obj->as.o->cap && j < n; i++) {
    if (obj->as.o->entries[i].key.k == V_STR) keys[j++] = ew_strdup(obj->as.o->entries[i].key.as.s->data);
  }
  keys[j] = NULL;
  return keys;
}

/* ------------------------------------------------------------------ */
/* value helpers                                                       */
/* ------------------------------------------------------------------ */

bool value_truthy(Value v) {
  switch (v.k) {
    case V_NULL: return false;
    case V_BOOL: return v.as.b;
    case V_NUM: return v.as.n != 0 && !isnan(v.as.n);
    case V_STR: return v.as.s && v.as.s->len > 0;
    case V_ARR: return true;
    case V_OBJ: return true;
    case V_FUN: return true;
  }
  return false;
}

bool value_equals(Value a, Value b) {
  if (a.k != b.k) {
    if (a.k == V_NUM && b.k == V_NUM) return a.as.n == b.as.n;
    return false;
  }
  switch (a.k) {
    case V_NULL: return true;
    case V_BOOL: return a.as.b == b.as.b;
    case V_NUM: return a.as.n == b.as.n;
    case V_STR: return strcmp(a.as.s->data, b.as.s->data) == 0;
    case V_ARR: {
      size_t n = arr_len(&a);
      if (n != arr_len(&b)) return false;
      for (size_t i = 0; i < n; i++) {
        Value x = arr_get(&a, i), y = arr_get(&b, i);
        if (!value_equals(x, y)) return false;
      }
      return true;
    }
    case V_OBJ: {
      if (obj_count(&a) != obj_count(&b)) return false;
      char** keys = obj_keys(&a);
      bool eq = true;
      for (int i = 0; keys[i]; i++) {
        Value x, y;
        if (!obj_get(&a, keys[i], &x) || !obj_get(&b, keys[i], &y)) {
          eq = false;
          break;
        }
        if (!value_equals(x, y)) {
          eq = false;
          value_release(&x);
          value_release(&y);
          break;
        }
        value_release(&x);
        value_release(&y);
      }
      for (int i = 0; keys[i]; i++) free(keys[i]);
      free(keys);
      return eq;
    }
    case V_FUN: return a.as.f == b.as.f;
  }
  return false;
}

int value_compare(Value a, Value b) {
  if (a.k == V_NUM && b.k == V_NUM) return a.as.n < b.as.n ? -1 : (a.as.n > b.as.n ? 1 : 0);
  if (a.k == V_STR && b.k == V_STR) return strcmp(a.as.s->data, b.as.s->data);
  double x = a.k == V_NUM ? a.as.n : (a.k == V_STR ? atof(a.as.s->data) : 0);
  double y = b.k == V_NUM ? b.as.n : (b.k == V_STR ? atof(b.as.s->data) : 0);
  return x < y ? -1 : (x > y ? 1 : 0);
}

char* value_to_string(Value v) {
  switch (v.k) {
    case V_NULL: return ew_strdup("null");
    case V_BOOL: return ew_strdup(v.as.b ? "true" : "false");
    case V_NUM: {
      double d = v.as.n;
      if (d == (double)(long long)d && d < 1e15 && d > -1e15) return ew_asprintf("%lld", (long long)d);
      return ew_asprintf("%.15g", d);
    }
    case V_STR: return ew_strdup(v.as.s->data);
    case V_ARR: {
      Buf b;
      buf_init(&b);
      buf_putc(&b, '[');
      for (size_t i = 0; i < arr_len(&v); i++) {
        if (i) buf_puts(&b, ", ");
        Value item = arr_get(&v, i);
        char* s = value_to_string(item);
        buf_puts(&b, s);
        free(s);
      }
      buf_putc(&b, ']');
      return buf_release(&b);
    }
    case V_OBJ: {
      Buf b;
      buf_init(&b);
      buf_putc(&b, '{');
      char** keys = obj_keys(&v);
      for (int i = 0; keys[i]; i++) {
        if (i) buf_puts(&b, ", ");
        buf_puts(&b, keys[i]);
        buf_puts(&b, ": ");
        Value val;
        if (obj_get(&v, keys[i], &val)) {
          char* s = value_to_string(val);
          buf_puts(&b, s);
          free(s);
          value_release(&val);
        }
        free(keys[i]);
      }
      free(keys);
      buf_putc(&b, '}');
      return buf_release(&b);
    }
    case V_FUN: return ew_asprintf("[function %s]", v.as.f->name ? v.as.f->name : "anonymous");
  }
  return ew_strdup("");
}

Value value_deep_copy(Value v) {
  switch (v.k) {
    case V_ARR: {
      Value out = arr_new();
      for (size_t i = 0; i < arr_len(&v); i++) {
        Value item = arr_get(&v, i);
        arr_push(&out, value_deep_copy(item));
      }
      return out;
    }
    case V_OBJ: {
      Value out = v_obj();
      char** keys = obj_keys(&v);
      for (int i = 0; keys[i]; i++) {
        Value val;
        if (obj_get(&v, keys[i], &val)) {
          Value cp = value_deep_copy(val);
          obj_set(&out, keys[i], cp);
          value_release(&cp);
          value_release(&val);
        }
        free(keys[i]);
      }
      free(keys);
      return out;
    }
    default:
      value_retain(&v);
      return v;
  }
}

Value value_index(Value obj, Value key) {
  if (obj.k == V_ARR) {
    if (key.k == V_NUM) {
      double d = key.as.n;
      if (d < 0) d += (double)arr_len(&obj);
      if (d < 0 || d != (double)(long long)d) return v_null();
      Value v = arr_get(&obj, (size_t)d);
      value_retain(&v); /* the caller owns this reference */
      return v;
    }
    if (key.k == V_STR) {
      double d = atof(key.as.s->data);
      if (d < 0) d += (double)arr_len(&obj);
      if (d < 0) return v_null();
      Value v = arr_get(&obj, (size_t)d);
      value_retain(&v);
      return v;
    }
    return v_null();
  }
  if (obj.k == V_OBJ) {
    if (key.k == V_STR) {
      Value out;
      if (obj_get(&obj, key.as.s->data, &out)) return out;
      return v_null();
    }
    if (key.k == V_NUM) {
      char* k = ew_asprintf("%lld", (long long)key.as.n);
      Value out;
      bool found = obj_get(&obj, k, &out);
      free(k);
      if (found) return out;
      return v_null();
    }
  }
  if (obj.k == V_STR) {
    if (key.k == V_NUM) {
      long long i = (long long)key.as.n;
      if (i < 0 || (size_t)i >= obj.as.s->len) return v_null();
      return v_strn(obj.as.s->data + i, 1);
    }
  }
  return v_null();
}

void value_set_index(Value obj, Value key, Value v) {
  if (obj.k == V_ARR) {
    if (key.k == V_NUM) {
      double d = key.as.n;
      if (d < 0) d += (double)arr_len(&obj);
      if (d < 0) return;
      arr_set(&obj, (size_t)d, v);
    }
    return;
  }
  if (obj.k == V_OBJ) {
    char* k = NULL;
    if (key.k == V_STR) {
      k = ew_strdup(key.as.s->data);
    } else if (key.k == V_NUM) {
      k = ew_asprintf("%lld", (long long)key.as.n);
    } else {
      char* s = value_to_string(key);
      k = s;
    }
    obj_set(&obj, k, v);
    free(k);
  }
}

/* ------------------------------------------------------------------ */
/* environments                                                        */
/* ------------------------------------------------------------------ */

Env* env_new(Env* parent) {
  Env* e = (Env*)ew_calloc(1, sizeof(Env));
  e->parent = parent;
  e->vars = obj_new();
  return e;
}

void env_free(Env* e) {
  /* only this scope - the parent is owned by whoever created it */
  if (!e) return;
  value_release(&e->vars);
  free(e);
}

bool env_get(Env* e, const char* name, Value* out) {
  for (Env* s = e; s; s = s->parent) {
    Value tmp;
    if (obj_get(&s->vars, name, &tmp)) {
      if (out) {
        *out = tmp;
        value_retain(out);
      }
      return true;
    }
  }
  return false;
}

void env_set(Env* e, const char* name, Value v) {
  for (Env* s = e; s; s = s->parent) {
    Value tmp;
    if (obj_get(&s->vars, name, &tmp)) {
      value_release(&tmp);
      obj_set(&s->vars, name, v);
      return;
    }
  }
  obj_set(&e->vars, name, v);
}

bool env_define(Env* e, const char* name, Value v) {
  obj_set(&e->vars, name, v);
  return true;
}
