/* easyweb - command line interface. */
#include "ew.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static void print_banner(void) {
  printf("\n");
  printf("  easyweb %s - small, fast and friendly web programming\n", EW_VERSION);
  printf("  --------------------------------------------------------------\n");
}

void ew_print_usage(void) {
  print_banner();
  printf("  USAGE\n");
  printf("    easyweb run [op] <file.eweb>     run your app on http://localhost:8080\n");
  printf("    easyweb build <file.eweb>        check the app and export a static page\n");
  printf("    easyweb check <file.eweb>        check the code without running it\n");
  printf("    easyweb highlight <file.eweb>    show the file with easyweb colours\n");
  printf("    easyweb new <name>               make a new easyweb project\n");
  printf("    easyweb packages                 list the packages you can install\n");
  printf("    easyweb webhelp <cmd>            the package manager\n");
  printf("    easyweb version                  show the version\n");
  printf("    easyweb help                     show this help\n");
  printf("\n  OPTIONS FOR run\n");
  printf("    --port <n>     use a different port (default 8080)\n");
  printf("    --host <ip>    bind to an ip (default 127.0.0.1)\n");
  printf("    --kid          friendly kid mode with soft errors\n");
  printf("    --no-open      do not open the browser\n");
  printf("    --op           open the browser when the server is ready\n");
  printf("\n  EXAMPLES\n");
  printf("    easyweb run op hello.eweb\n");
  printf("    easyweb run --port 3000 myapp.eweb\n");
  printf("    webhelp install animateeasy\n");
  printf("\n");
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static bool file_has_suffix(const char* path, const char* suffix) {
  const char* ext = path_ext(path);
  bool ok = strcmp(ext, suffix) == 0;
  free((void*)ext);
  return ok;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */
/* ------------------------------------------------------------------ */

int cmd_run(int argc, char** argv) {
  const char* file = NULL;
  int port = -1;
  const char* host = "127.0.0.1";
  bool kid = false;
  bool open = false;
  bool no_open = false;

  for (int i = 0; i < argc; i++) {
    const char* a = argv[i];
    if (strcmp(a, "--port") == 0 && i + 1 < argc) {
      port = atoi(argv[++i]);
    } else if (strcmp(a, "--host") == 0 && i + 1 < argc) {
      host = argv[++i];
    } else if (strcmp(a, "--kid") == 0 || strcmp(a, "--kidmode") == 0) {
      kid = true;
    } else if (strcmp(a, "--op") == 0 || strcmp(a, "--open") == 0) {
      open = true;
    } else if (strcmp(a, "--no-open") == 0) {
      no_open = true;
    } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
      ew_print_usage();
      return 0;
    } else if (a[0] == '-') {
      fprintf(stderr, "easyweb: unknown option '%s'\n", a);
      return 2;
    } else if (!file) {
      file = a;
    } else if (strcmp(a, "op") == 0 || strcmp(a, "open") == 0) {
      open = true;
    } else {
      fprintf(stderr, "easyweb: too many arguments\n");
      return 2;
    }
  }

  if (!file) {
    fprintf(stderr, "easyweb: which file should I run? Try: easyweb run op hello.eweb\n");
    return 2;
  }
  if (!file_has_suffix(file, "eweb")) {
    fprintf(stderr, "easyweb: '%s' is not an easyweb file (it should end with .eweb)\n", file);
    return 2;
  }
  if (!path_exists(file)) {
    fprintf(stderr, "easyweb: I cannot find '%s'\n", file);
    return 2;
  }

  /* peek for kid mode so errors can be friendly */
  if (!kid) {
    char* src = NULL;
    size_t len = 0;
    if (path_read_file(file, &src, &len)) {
      if (strstr(src, "kid {") || strstr(src, "kidmode on")) kid = true;
      free(src);
    }
  }

  Interp* in = interp_new(kid);
  Diag d;
  diag_init(&d);
  if (!interp_load_file(in, file, &d)) {
    if (kid) {
      diag_print_friendly(&d, diag_ansi_enabled());
    } else {
      diag_print(&d, diag_ansi_enabled());
    }
    diag_free(&d);
    interp_free(in);
    return 1;
  }
  diag_free(&d);

  ServerOpts opts;
  memset(&opts, 0, sizeof(opts));
  opts.host = ew_strdup(host);
  opts.port = (port > 0) ? port : in->port;
  opts.interp = in;
  opts.app_path = ew_strdup(file);
  opts.open_browser = open && !no_open;
  opts.kidmode = kid;
  opts.max_threads = 64;

  int rc = server_run(&opts);

  free(opts.host);
  free(opts.app_path);
  interp_free(in);
  return rc;
}

int cmd_check(int argc, char** argv) {
  const char* file = NULL;
  bool kid = false;
  for (int i = 0; i < argc; i++) {
    const char* a = argv[i];
    if (strcmp(a, "--kid") == 0) kid = true;
    else if (a[0] == '-') continue;
    else if (!file) file = a;
  }
  if (!file) {
    fprintf(stderr, "easyweb: which file should I check?\n");
    return 2;
  }
  if (!path_exists(file)) {
    fprintf(stderr, "easyweb: I cannot find '%s'\n", file);
    return 2;
  }
  char* src = NULL;
  size_t len = 0;
  if (!path_read_file(file, &src, &len)) {
    fprintf(stderr, "easyweb: I cannot read '%s'\n", file);
    return 2;
  }
  if (!kid && (strstr(src, "kid {") || strstr(src, "kidmode on"))) kid = true;

  Parser p;
  parser_init(&p, src, len, file, kid);
  Node* prog = parser_parse(&p);
  if (!prog) {
    if (kid) diag_print_friendly(&p.diag, diag_ansi_enabled());
    else diag_print(&p.diag, diag_ansi_enabled());
    free(src);
    return 1;
  }
  node_free(prog);
  free(src);
  printf("  %s is looking good. No problems found.\n", file);
  return 0;
}

int cmd_build(int argc, char** argv) {
  const char* file = NULL;
  const char* out = NULL;
  for (int i = 0; i < argc; i++) {
    const char* a = argv[i];
    if (strcmp(a, "-o") == 0 && i + 1 < argc) out = argv[++i];
    else if (strcmp(a, "--out") == 0 && i + 1 < argc) out = argv[++i];
    else if (a[0] == '-') continue;
    else if (!file) file = a;
  }
  if (!file) {
    fprintf(stderr, "easyweb: which file should I build?\n");
    return 2;
  }
  if (!path_exists(file)) {
    fprintf(stderr, "easyweb: I cannot find '%s'\n", file);
    return 2;
  }
  bool kid = false;
  char* src = NULL;
  size_t len = 0;
  if (path_read_file(file, &src, &len)) {
    if (strstr(src, "kid {") || strstr(src, "kidmode on")) kid = true;
  }

  Interp* in = interp_new(kid);
  Diag d;
  diag_init(&d);
  if (!interp_load_file(in, file, &d)) {
    if (kid) diag_print_friendly(&d, diag_ansi_enabled());
    else diag_print(&d, diag_ansi_enabled());
    diag_free(&d);
    interp_free(in);
    free(src);
    return 1;
  }
  diag_free(&d);

  Env* env = env_new(in->globals);
  char* html = render_page(in, env);
  env_free(env);

  char* outpath;
  if (out) {
    outpath = ew_strdup(out);
  } else {
    char* stem = path_strip_ext(file);
    outpath = ew_asprintf("%s.html", stem);
    free(stem);
  }
  bool ok = path_write_file(outpath, html, strlen(html));
  printf("  built %s (%zu bytes)\n", outpath, strlen(html));
  free(outpath);
  free(html);
  free(src);
  interp_free(in);
  return ok ? 0 : 1;
}

int cmd_highlight(int argc, char** argv) {
  const char* file = NULL;
  bool html = false;
  for (int i = 0; i < argc; i++) {
    const char* a = argv[i];
    if (strcmp(a, "--html") == 0) html = true;
    else if (strcmp(a, "-h") == 0) {
      printf("  easyweb highlight <file.eweb> [--html]\n");
      return 0;
    } else if (a[0] == '-') continue;
    else if (!file) file = a;
  }
  if (!file) {
    fprintf(stderr, "easyweb: which file should I colour in?\n");
    return 2;
  }
  char* src = NULL;
  size_t len = 0;
  if (!path_read_file(file, &src, &len)) {
    fprintf(stderr, "easyweb: I cannot read '%s'\n", file);
    return 2;
  }
  if (html) {
    char* out = highlight_html(src, len, true);
    printf("%s", out);
    free(out);
  } else {
    char* out = highlight_ansi(src, len);
    printf("%s", out);
    free(out);
  }
  free(src);
  return 0;
}

int cmd_packages(int argc, char** argv) {
  (void)argc;
  (void)argv;
  PackageList* reg = pkg_builtin_registry();
  printf("\n  Packages you can install with 'webhelp install <name>':\n\n");
  for (int i = 0; i < reg->count; i++) {
    Package* p = &reg->items[i];
    printf("    %-14s %-8s %s\n", p->name, p->version, p->description);
  }
  printf("\n");
  pkg_list_free(reg);
  return 0;
}

int cmd_version(int argc, char** argv) {
  (void)argc;
  (void)argv;
  printf("easyweb %s\n", EW_VERSION);
  return 0;
}

int cmd_help(int argc, char** argv) {
  (void)argc;
  (void)argv;
  ew_print_usage();
  return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char** argv) {
  ew_print_ansi(true);

  if (argc < 2) {
    ew_print_usage();
    return 0;
  }

  const char* cmd = argv[1];
  if (strcmp(cmd, "run") == 0 || strcmp(cmd, "serve") == 0 || strcmp(cmd, "start") == 0) {
    return cmd_run(argc - 2, argv + 2);
  }
  if (strcmp(cmd, "build") == 0) return cmd_build(argc - 2, argv + 2);
  if (strcmp(cmd, "check") == 0) return cmd_check(argc - 2, argv + 2);
  if (strcmp(cmd, "highlight") == 0 || strcmp(cmd, "colour") == 0) return cmd_highlight(argc - 2, argv + 2);
  if (strcmp(cmd, "new") == 0 || strcmp(cmd, "create") == 0) return cmd_new(argc - 2, argv + 2);
  if (strcmp(cmd, "packages") == 0) return cmd_packages(argc - 2, argv + 2);
  if (strcmp(cmd, "webhelp") == 0) return webhelp_main(argc - 2, argv + 2);
  if (strcmp(cmd, "version") == 0 || strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0)
    return cmd_version(argc - 2, argv + 2);
  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0)
    return cmd_help(argc - 2, argv + 2);

  fprintf(stderr, "easyweb: unknown command '%s'\n", cmd);
  ew_print_usage();
  return 2;
}
