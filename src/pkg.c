/* easyweb - package manager core (used by webhelp and the runtime). */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static PackageList* pkg_list_new(void) {
  return (PackageList*)ew_calloc(1, sizeof(PackageList));
}

static void pkg_list_add(PackageList* l, Package p) {
  if (l->count == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 8;
    l->items = (Package*)ew_realloc(l->items, sizeof(Package) * (size_t)l->cap);
  }
  l->items[l->count++] = p;
}

void pkg_list_free(PackageList* l) {
  if (!l) return;
  for (int i = 0; i < l->count; i++) {
    free(l->items[i].name);
    free(l->items[i].version);
    free(l->items[i].description);
    free(l->items[i].entry);
    free(l->items[i].css);
    free(l->items[i].js);
    free(l->items[i].dir);
  }
  free(l->items);
  free(l);
}

Package* pkg_find(PackageList* l, const char* name) {
  if (!l || !name) return NULL;
  for (int i = 0; i < l->count; i++) {
    if (strcmp(l->items[i].name, name) == 0) return &l->items[i];
  }
  return NULL;
}

PackageList* pkg_search(PackageList* l, const char* query) {
  PackageList* out = pkg_list_new();
  if (!l) return out;
  if (!query || !*query) {
    for (int i = 0; i < l->count; i++) pkg_list_add(out, l->items[i]);
    return out;
  }
  for (int i = 0; i < l->count; i++) {
    Package* p = &l->items[i];
    const char* hay = p->description ? p->description : "";
    if (strstr(p->name, query) || strstr(hay, query)) pkg_list_add(out, *p);
  }
  return out;
}

/* ------------------------------------------------------------------ */
/* manifest reading                                                    */
/* ------------------------------------------------------------------ */

/* pulls "name" : "value" out of a small json manifest */
static bool manifest_field(const char* text, const char* field, char** out) {
  if (!text || !field) return false;
  char* pat = ew_asprintf("\"%s\"", field);
  char* p = strstr(text, pat);
  free(pat);
  if (!p) return false;
  p += strlen(field) + 2;
  while (*p && *p != ':') p++;
  if (!*p) return false;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (*p != '"') return false;
  p++;
  Buf b;
  buf_init(&b);
  while (*p && *p != '"') {
    if (*p == '\\' && p[1]) p++;
    buf_putc(&b, *p++);
  }
  *out = buf_release(&b);
  return true;
}

char* pkg_manifest_json(const char* dir) {
  /* returns the whole manifest.json text (or NULL) */
  char* path = path_join(dir, "manifest.json");
  char* data = NULL;
  size_t len = 0;
  char* out = NULL;
  if (path_read_file(path, &data, &len)) out = ew_strndup(data, len);
  free(data);
  free(path);
  return out;
}

static Package pkg_from_dir(const char* dir, const char* name) {
  Package p;
  memset(&p, 0, sizeof(p));
  p.name = ew_strdup(name);
  p.dir = ew_strdup(dir);
  p.builtin = false;
  p.installed = true;

  char* mpath = path_join(dir, "manifest.json");
  char* data = NULL;
  size_t len = 0;
  if (path_read_file(mpath, &data, &len)) {
    char* v = NULL;
    if (manifest_field(data, "version", &v)) { p.version = v; v = NULL; }
    if (manifest_field(data, "description", &v)) { p.description = v; v = NULL; }
    if (manifest_field(data, "entry", &v)) { p.entry = v; v = NULL; }
    if (manifest_field(data, "css", &v)) { p.css = v; v = NULL; }
    if (manifest_field(data, "js", &v)) { p.js = v; v = NULL; }
    free(data);
  }
  free(mpath);
  if (!p.version) p.version = ew_strdup("0.0.0");
  if (!p.description) p.description = ew_strdup("");
  if (!p.entry) p.entry = ew_asprintf("%s.eweb", name);
  if (!p.css) p.css = ew_strdup("");
  if (!p.js) p.js = ew_strdup("");
  return p;
}

char* pkg_dir_project(const char* project_dir) {
  char* base = path_join(project_dir ? project_dir : ".", ".easyweb");
  char* out = path_join(base, "packages");
  free(base);
  return out;
}

char* pkg_dir_global(void) {
  char* home = path_home();
  char* base = path_join(home, ".easyweb");
  free(home);
  char* out = path_join(base, "packages");
  free(base);
  return out;
}

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

PackageList* pkg_builtin_registry(void) {
  PackageList* l = pkg_list_new();
  int count = 0;
  const EmbeddedPkg* eps = embedded_packages(&count);
  for (int i = 0; i < count; i++) {
    Package p;
    memset(&p, 0, sizeof(p));
    p.name = ew_strdup(eps[i].name);
    p.version = ew_strdup(eps[i].version);
    p.description = ew_strdup(eps[i].description);
    p.entry = ew_strdup(eps[i].entry_file ? eps[i].entry_file : "");
    p.css = ew_strdup(eps[i].css_file ? eps[i].css_file : "");
    p.js = ew_strdup(eps[i].js_file ? eps[i].js_file : "");
    p.builtin = true;
    p.installed = false;
    p.dir = NULL;
    pkg_list_add(l, p);
  }
  return l;
}

PackageList* pkg_installed(const char* project_dir) {
  PackageList* l = pkg_list_new();
  const char* dirs[2];
  char* pdir = pkg_dir_project(project_dir);
  char* gdir = pkg_dir_global();
  dirs[0] = pdir;
  dirs[1] = gdir;

  for (int d = 0; d < 2; d++) {
    if (!path_is_dir(dirs[d])) continue;
#ifdef _WIN32
    char* pat = path_join(dirs[d], "*");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char* sub = path_join(dirs[d], fd.cFileName);
        if (path_is_dir(sub)) {
          Package p = pkg_from_dir(sub, fd.cFileName);
          pkg_list_add(l, p);
        }
        free(sub);
      } while (FindNextFileA(h, &fd));
      FindClose(h);
    }
    free(pat);
#else
    DIR* dp = opendir(dirs[d]);
    if (dp) {
      struct dirent* de;
      while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char* sub = path_join(dirs[d], de->d_name);
        if (path_is_dir(sub)) {
          Package p = pkg_from_dir(sub, de->d_name);
          pkg_list_add(l, p);
        }
        free(sub);
      }
      closedir(dp);
    }
#endif
  }
  free(pdir);
  free(gdir);
  return l;
}

/* ------------------------------------------------------------------ */
/* loading package files                                               */
/* ------------------------------------------------------------------ */

static const EmbeddedPkg* find_embedded(const char* name) {
  int count = 0;
  const EmbeddedPkg* eps = embedded_packages(&count);
  for (int i = 0; i < count; i++) {
    if (strcmp(eps[i].name, name) == 0) return &eps[i];
  }
  return NULL;
}

char* pkg_load_entry(Package* p, size_t* len) {
  if (!p) return NULL;
  if (p->builtin) {
    const EmbeddedPkg* e = find_embedded(p->name);
    if (!e || !e->entry_code) return NULL;
    *len = strlen(e->entry_code);
    return ew_strdup(e->entry_code);
  }
  if (!p->dir || !p->entry) return NULL;
  char* file = path_join(p->dir, p->entry);
  char* data = NULL;
  size_t n = 0;
  if (!path_read_file(file, &data, &n)) {
    free(file);
    return NULL;
  }
  free(file);
  *len = n;
  return data;
}

char* pkg_load_asset(Package* p, const char* which, size_t* len) {
  if (!p) return NULL;
  if (p->builtin) {
    const EmbeddedPkg* e = find_embedded(p->name);
    if (!e) return NULL;
    const char* src = NULL;
    if (strcmp(which, "css") == 0) src = e->css_code;
    else if (strcmp(which, "js") == 0) src = e->js_code;
    if (!src || !*src) return NULL;
    *len = strlen(src);
    return ew_strdup(src);
  }
  const char* fname = NULL;
  if (strcmp(which, "css") == 0) fname = p->css;
  else if (strcmp(which, "js") == 0) fname = p->js;
  if (!p->dir || !fname || !*fname) return NULL;
  char* file = path_join(p->dir, fname);
  char* data = NULL;
  size_t n = 0;
  if (!path_read_file(file, &data, &n)) {
    free(file);
    return NULL;
  }
  free(file);
  *len = n;
  return data;
}

/* ------------------------------------------------------------------ */
/* install / remove                                                    */
/* ------------------------------------------------------------------ */

bool pkg_install(const char* name, const char* project_dir, bool global, char* err, size_t errsz) {
  if (!name || !*name) {
    if (err) snprintf(err, errsz, "no package name given");
    return false;
  }
  const EmbeddedPkg* e = find_embedded(name);
  if (!e) {
    if (err)
      snprintf(err, errsz,
               "package '%s' was not found. Run 'webhelp search' to see what is available", name);
    return false;
  }
  char* base = global ? pkg_dir_global() : pkg_dir_project(project_dir);
  char* dir = path_join(base, name);
  if (!path_mkdir_p(dir)) {
    if (err) snprintf(err, errsz, "could not create the folder %s", dir);
    free(base);
    free(dir);
    return false;
  }

  /* manifest */
  char* manifest = ew_asprintf(
      "{\n  \"name\": \"%s\",\n  \"version\": \"%s\",\n  \"description\": \"%s\",\n"
      "  \"entry\": \"%s\",\n  \"css\": \"%s\",\n  \"js\": \"%s\"\n}\n",
      e->name, e->version, e->description, e->entry_file ? e->entry_file : "", e->css_file ? e->css_file : "",
      e->js_file ? e->js_file : "");
  char* mpath = path_join(dir, "manifest.json");
  path_write_file(mpath, manifest, strlen(manifest));
  free(mpath);
  free(manifest);

  struct {
    const char* file;
    const char* body;
  } files[] = {
      {e->entry_file, e->entry_code},
      {e->css_file, e->css_code},
      {e->js_file, e->js_code},
  };
  for (int i = 0; i < 3; i++) {
    if (!files[i].file || !*files[i].file || !files[i].body) continue;
    char* out = path_join(dir, files[i].file);
    path_write_file(out, files[i].body, strlen(files[i].body));
    free(out);
  }

  /* record it in the project lock file */
  if (!global) {
    char* lock = path_join(project_dir ? project_dir : ".", "easyweb_packages.txt");
    bool found = false;
    char* data = NULL;
    size_t len = 0;
    if (path_read_file(lock, &data, &len)) {
      if (strstr(data, name)) found = true;
      free(data);
    }
    if (!found) {
      FILE* f = fopen(lock, "ab");
      if (f) {
        fprintf(f, "%s %s\n", name, e->version);
        fclose(f);
      }
    }
    free(lock);
  }

  free(base);
  free(dir);
  return true;
}

bool pkg_remove(const char* name, const char* project_dir, bool global, char* err, size_t errsz) {
  char* base = global ? pkg_dir_global() : pkg_dir_project(project_dir);
  char* dir = path_join(base, name);
  bool ok = path_is_dir(dir);
  if (ok) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char* pat = path_join(dir, "*");
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char* sub = path_join(dir, fd.cFileName);
        remove(sub);
        free(sub);
      } while (FindNextFileA(h, &fd));
      FindClose(h);
    }
    free(pat);
    RemoveDirectoryA(dir);
#else
    DIR* dp = opendir(dir);
    if (dp) {
      struct dirent* de;
      while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char* sub = path_join(dir, de->d_name);
        remove(sub);
        free(sub);
      }
      closedir(dp);
    }
    rmdir(dir);
#endif
  } else {
    if (err) snprintf(err, errsz, "package '%s' is not installed", name);
  }
  if (ok && !global) {
    char* lock = path_join(project_dir ? project_dir : ".", "easyweb_packages.txt");
    char* data = NULL;
    size_t len = 0;
    if (path_read_file(lock, &data, &len)) {
      Buf b;
      buf_init(&b);
      const char* p = data;
      while (*p) {
        const char* nl = strchr(p, '\n');
        size_t l = nl ? (size_t)(nl - p) : strlen(p);
        char* line = ew_strndup(p, l);
        char* sp = strchr(line, ' ');
        bool skip = false;
        if (sp) {
          *sp = 0;
          if (strcmp(line, name) == 0) skip = true;
        }
        if (!skip && *line) {
          buf_puts(&b, line);
          buf_putc(&b, ' ');
          buf_puts(&b, sp ? sp + 1 : "");
          buf_putc(&b, '\n');
        }
        free(line);
        if (!nl) break;
        p = nl + 1;
      }
      path_write_file(lock, b.data ? b.data : "", b.len);
      buf_free(&b);
      free(data);
    }
    free(lock);
  }
  free(base);
  free(dir);
  return ok;
}
