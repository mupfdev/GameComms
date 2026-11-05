#!/bin/sh
# Build the libFuzzer harnesses (clang required):  sh server/fuzz/build.sh [outdir]
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
O=${1:-$R/build/fuzz}
mkdir -p "$O"
F="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -I$R/protocol/include -I$R/server/src"
P="$R/protocol/src/gc_codec.c $R/protocol/src/gc_link.c $R/protocol/src/gc_msg.c"
clang $F -o "$O/fuzz_protocol" "$R/server/fuzz/fuzz_protocol.c" $P
clang $F -o "$O/fuzz_core" "$R/server/fuzz/fuzz_core.c" "$R/server/src/gcs_core.c" $P
echo "built $O/fuzz_protocol $O/fuzz_core"
