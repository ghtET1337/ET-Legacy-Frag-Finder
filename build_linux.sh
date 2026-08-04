#!/usr/bin/env bash
set -euo pipefail

compiler="${CXX:-g++}"
c_compiler="${CC:-cc}"
mkdir -p build

"$c_compiler" -std=c99 -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION \
  -c third_party/sqlite/sqlite3.c -o build/sqlite3.o

"$compiler" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc \
  src/etl_demo_parser.cpp src/idtech3_huffman.cpp src/app_storage.cpp src/main_cli.cpp \
  build/sqlite3.o -ldl -pthread -o build/etl-frag-cli

"$compiler" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc \
  src/etl_demo_parser.cpp src/idtech3_huffman.cpp src/app_storage.cpp tests/test_frag_runs.cpp \
  build/sqlite3.o -ldl -pthread -o build/etl-frag-tests

./build/etl-frag-tests
echo "Ready: build/etl-frag-cli"
