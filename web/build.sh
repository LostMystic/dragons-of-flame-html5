#!/bin/bash
# Browser build into build-web/web/, then the packs (tools/web/pack.py: files/*.pack and version.json).
# Needs the Emscripten SDK; by default the copy in the Fantasy Empires project.
# Test locally: python3 -m http.server 8080 --directory build-web/web   then open http://127.0.0.1:8080/
set -e
cd "$(dirname "$0")/.."
EMSDK=${EMSDK:-$PWD/../fantemp/work/emsdk}
source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-web -j"$(nproc)"
W=build-web/web
rm -f $W/dragons-of-flame.data
cp -f web/static/* $W/
cp -f $W/dragons-of-flame.html $W/index.html   # (the server calls it index.html)
python3 tools/web/pack.py $W $W/dragons-of-flame.html $W/dragons-of-flame.js $W/dragons-of-flame.wasm $W/packs.js $W/exit.html
ls -la $W
