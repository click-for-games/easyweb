/* easyweb - webhelp, the easyweb package manager. */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

static void webhelp_usage(void) {
  printf("\n");
  printf("  webhelp %s - the easyweb package manager\n", EW_VERSION);
  printf("  --------------------------------------------------------------\n");
  printf("  USAGE\n");
  printf("    webhelp install <package>   install a package into this project\n");
  printf("    webhelp install <package> --global   install it for every project\n");
  printf("    webhelp remove <package>    remove a package\n");
  printf("    webhelp list                show the installed packages\n");
  printf("    webhelp search [word]       look for a package\n");
  printf("    webhelp info <package>      show details about a package\n");
  printf("    webhelp init [name]         make a new easyweb project\n");
  printf("    webhelp version             show the version\n");
  printf("    webhelp help                show this help\n");
  printf("\n  EXAMPLES\n");
  printf("    webhelp install animateeasy\n");
  printf("    webhelp install dataweb\n");
  printf("\n");
}

static const char* project_dir(void) {
  static char* cached = NULL;
  if (!cached) cached = path_cwd();
  return cached;
}

static char* starter_app(const char* name) {
  return ew_asprintf(
      "page \"%s\"\n"
      "port 8080\n"
      "\n"
      "// your data lives here\n"
      "data {\n"
      "  visits: 0\n"
      "}\n"
      "\n"
      "// a server route - the backend\n"
      "route GET \"/api/hello\" {\n"
      "  data.visits = data.visits + 1\n"
      "  return { hello: \"world\", visits: data.visits }\n"
      "}\n"
      "\n"
      "// the page - the frontend\n"
      "ui {\n"
      "  <h1>Hello from %s!</h1>\n"
      "  <p>This page was made with easyweb.</p>\n"
      "  <button id=\"go\">Click me</button>\n"
      "  <pre id=\"out\"></pre>\n"
      "}\n"
      "\n"
      "// the logic - runs in the browser\n"
      "script {\n"
      "  on(\"#go\", \"click\", function() {\n"
      "    get(\"/api/hello\", function(res) {\n"
      "      text(\"#out\", json(res))\n"
      "    })\n"
      "  })\n"
      "}\n",
      name, name);
}

int cmd_new(int argc, char** argv) {
  const char* name = NULL;
  for (int i = 0; i < argc; i++) {
    if (argv[i][0] == '-') continue;
    if (!name) name = argv[i];
  }
  if (!name) {
    fprintf(stderr, "easyweb: what should I call your project?\n");
    return 2;
  }
  char* dir = ew_strdup(name);
  if (!path_mkdir_p(dir)) {
    fprintf(stderr, "easyweb: could not make the folder '%s'\n", dir);
    free(dir);
    return 1;
  }
  char* file = ew_asprintf("%s/%s.eweb", dir, name);
  char* code = starter_app(name);
  if (path_exists(file)) {
    fprintf(stderr, "easyweb: '%s' already exists\n", file);
    free(file);
    free(code);
    free(dir);
    return 1;
  }
  path_write_file(file, code, strlen(code));
  printf("\n  Made a new easyweb project: %s\n", dir);
  printf("  Run it with:  easyweb run op %s\n\n", file);
  free(file);
  free(code);
  free(dir);
  return 0;
}

int webhelp_main(int argc, char** argv) {
  ew_print_ansi(true);
  if (argc < 1) {
    webhelp_usage();
    return 0;
  }
  const char* cmd = argv[0];

  if (strcmp(cmd, "install") == 0 || strcmp(cmd, "i") == 0 || strcmp(cmd, "add") == 0) {
    if (argc < 2) {
      fprintf(stderr, "webhelp: which package should I install?\n");
      return 2;
    }
    bool global = false;
    const char* name = NULL;
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--global") == 0 || strcmp(argv[i], "-g") == 0) global = true;
      else if (!name) name = argv[i];
    }
    char err[256];
    err[0] = 0;
    if (!pkg_install(name, project_dir(), global, err, sizeof(err))) {
      fprintf(stderr, "webhelp: %s\n", err[0] ? err : "install failed");
      return 1;
    }
    printf("\n  installed %s", name);
    if (global) {
      printf(" (global)\n");
    } else {
      printf(" -> %s\n", pkg_dir_project(project_dir()));
    }
    printf("  now add this line to your app:\n");
    printf("      use %s\n\n", name);
    return 0;
  }

  if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "rm") == 0 || strcmp(cmd, "uninstall") == 0) {
    if (argc < 2) {
      fprintf(stderr, "webhelp: which package should I remove?\n");
      return 2;
    }
    bool global = false;
    const char* name = NULL;
    for (int i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--global") == 0 || strcmp(argv[i], "-g") == 0) global = true;
      else if (!name) name = argv[i];
    }
    char err[256];
    err[0] = 0;
    if (!pkg_remove(name, project_dir(), global, err, sizeof(err))) {
      fprintf(stderr, "webhelp: %s\n", err[0] ? err : "remove failed");
      return 1;
    }
    printf("\n  removed %s\n\n", name);
    return 0;
  }

  if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) {
    PackageList* installed = pkg_installed(project_dir());
    printf("\n  installed in this project:\n");
    if (installed->count == 0) {
      printf("    (nothing yet - try: webhelp install animateeasy)\n");
    }
    for (int i = 0; i < installed->count; i++) {
      Package* p = &installed->items[i];
      printf("    %-16s %-8s %s\n", p->name, p->version, p->description);
    }
    printf("\n");
    pkg_list_free(installed);
    return 0;
  }

  if (strcmp(cmd, "search") == 0) {
    const char* query = (argc > 1) ? argv[1] : NULL;
    PackageList* reg = pkg_builtin_registry();
    PackageList* found = pkg_search(reg, query);
    printf("\n  packages:\n");
    if (found->count == 0) printf("    (nothing found)\n");
    for (int i = 0; i < found->count; i++) {
      Package* p = &found->items[i];
      printf("    %-16s %-8s %s\n", p->name, p->version, p->description);
    }
    printf("\n");
    pkg_list_free(found);
    pkg_list_free(reg);
    return 0;
  }

  if (strcmp(cmd, "info") == 0) {
    if (argc < 2) {
      fprintf(stderr, "webhelp: which package?\n");
      return 2;
    }
    PackageList* reg = pkg_builtin_registry();
    Package* p = pkg_find(reg, argv[1]);
    if (!p) {
      fprintf(stderr, "webhelp: no package called '%s'\n", argv[1]);
      pkg_list_free(reg);
      return 1;
    }
    printf("\n  %s %s\n", p->name, p->version);
    printf("  %s\n", p->description);
    printf("\n  install with:  webhelp install %s\n", p->name);
    printf("  use with:      use %s\n\n", p->name);
    pkg_list_free(reg);
    return 0;
  }

  if (strcmp(cmd, "init") == 0 || strcmp(cmd, "new") == 0) {
    const char* name = (argc > 1) ? argv[1] : "myapp";
    char* args[2];
    args[0] = (char*)name;
    return cmd_new(1, args);
  }

  if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0) {
    printf("webhelp %s\n", EW_VERSION);
    return 0;
  }

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
    webhelp_usage();
    return 0;
  }

  fprintf(stderr, "webhelp: unknown command '%s'\n", cmd);
  webhelp_usage();
  return 2;
}
