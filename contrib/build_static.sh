#!/bin/sh
set -e   # if an error occurs, close it immediately.
set -x   # print runing line command.
srcdir="$(dirname $0)"  # save shell script run directory.
cd "$srcdir/../depends"
make -j 4
cd ..
sh ./autogen.sh
./configure --enable-glibc-back-compat --with-qrencode --prefix=`pwd`/depends/x86_64-pc-linux-gnu LDFLAGS="-static-libstdc++"
make -j 4
