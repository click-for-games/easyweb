#!/bin/sh
# easyweb build script for macOS and Linux
#
#   ./build.sh
#
set -e

CC=${CC:-gcc}
DIST=${DIST:-dist}
CFLAGS="-O2 -std=c11 -Wall -Wextra"
LDLIBS="-lm"

CORE="src/util.c src/lexer.c src/ast.c src/parser.c src/value.c \
src/interp.c src/jsgen.c src/kid.c src/render.c src/http.c \
src/server.c src/pkg.c src/packages.c src/highlight.c src/json.c \
src/webhelp.c"

mkdir -p "$DIST"

echo "==> building easyweb"
$CC $CFLAGS -o "$DIST/easyweb" $CORE src/main.c $LDLIBS

echo "==> building webhelp"
$CC $CFLAGS -o "$DIST/webhelp" $CORE src/webhelp_main.c $LDLIBS

echo "==> done"
ls -l "$DIST"
