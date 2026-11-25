#!/bin/bash
# Rebuild every workload's eden_swappf_<tag>.ko from its saved tree headers against the
# current kernel tree (needed after kernel #6: the swap_prefetch_req change alters the
# symbol CRCs, so #5-built modules don't load). The #5 build is kept as <name>.ko.k5.
set -eu
K=/home/shaurya/eden/tools/kswappf
for ko in /data1/shaurya/deku_*/linux66b/eden_swappf_n*_c*.ko; do
  [ -L "$ko" ] && continue                      # symlinks follow their target
  d=$(dirname $ko); tag=$(basename $ko .ko); tag=${tag#eden_swappf_}
  [ -f $d/headers_$tag/native_tree_model_spatial.h ] || { echo "skip $ko (no headers)"; continue; }
  [ -f $ko.k5 ] || cp $ko $ko.k5
  flock /data1/shaurya/swappf_build.lock sh -c "cd $K; make clean >/dev/null 2>&1; make TREES=$d/headers_$tag 2>&1 | grep -E 'error|warning' || true; cp eden_swappf.ko $ko; make clean >/dev/null 2>&1"
  echo "rebuilt $ko"
done
