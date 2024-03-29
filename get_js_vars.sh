#!/bin/sh
PREFIX="/usr/local/include"

FILES="${PREFIX}/gavl/metatags.h ${PREFIX}/gavl/msg.h ${PREFIX}/gmerlin/bgmsg.h ${PREFIX}/gmerlin/playermsg.h ${PREFIX}/gmerlin/mdb.h"

for i in $FILES; do
  awk '(NF >= 3) && ($1 == "#define") {
    value = substr($0,index($0,$3))
    idx = index(value, "//")
    if(idx > 0)
      value = substr(value, 1, idx-1);
    idx = index(value, "/*");
    if(idx > 0)
      value = substr(value, 1, idx-1);
    printf "const %s = %s;\n", $2, value;
    }' $i
done

