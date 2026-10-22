# easyweb - makefile for macOS and Linux
#
#   make            build easyweb and webhelp into dist/
#   make clean      remove the build output
#
CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
LDLIBS  ?= -lm
DIST    ?= dist

CORE = src/util.c src/lexer.c src/ast.c src/parser.c src/value.c \
       src/interp.c src/jsgen.c src/kid.c src/render.c src/http.c \
       src/server.c src/pkg.c src/packages.c src/highlight.c src/json.c \
       src/webhelp.c

.PHONY: all clean

all: $(DIST)/easyweb $(DIST)/webhelp

$(DIST):
	mkdir -p $(DIST)

$(DIST)/easyweb: $(CORE) src/main.c | $(DIST)
	$(CC) $(CFLAGS) -o $@ $(CORE) src/main.c $(LDLIBS)

$(DIST)/webhelp: $(CORE) src/webhelp_main.c | $(DIST)
	$(CC) $(CFLAGS) -o $@ $(CORE) src/webhelp_main.c $(LDLIBS)

clean:
	rm -rf $(DIST)
