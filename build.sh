#!/bin/bash

MAKE_TOOL=make
if ! [ -x "$(command -v make)" ]; then
    MAKE_TOOL=mingw32-make
else
    MAKE_TOOL=make
fi

$MAKE_TOOL clean
$MAKE_TOOL \
    CC=x86_64-w64-mingw32-gcc \
    HOST=x86_64-windows \
    CFLAGS='-O2 -Wall -W -Wno-format -Wno-cast-function-type -Wno-unused-function -Wno-parentheses -Wstrict-prototypes -Wmissing-prototypes' \
    LDFLAGS='-static' \
    ZLIB=yes \
    IDSDIR="" \
    COMPAT_GETOPT=yes \
    $@
