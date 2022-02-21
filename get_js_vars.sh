#!/bin/sh
PREFIX="/usr/local/include"

FILES="${PREFIX}/gavl/metatags.h ${PREFIX}/gavl/msg.h ${PREFIX}/gmerlin/msgqueue.h ${PREFIX}/gmerlin/playermsg.h"

for i in $FILES; do
  awk '(NF >= 3) && ($1 == "#define") { printf "const %s = %s;\n", $2, $3 }' $i
done

