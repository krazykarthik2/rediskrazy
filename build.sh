#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$SCRIPT_DIR/src_c"
BACKEND_DIR="$SOURCE_DIR/backend"
QP_DIR="$SOURCE_DIR/query_processor"
UI_DIR="$SOURCE_DIR/UI"
OUTPUT_DIR="$SCRIPT_DIR/execs"

echo "===================================================="
echo "STEP 0: Checking prerequisites and preparing output"
echo "===================================================="

command -v gcc >/dev/null 2>&1 || { echo "[ERROR] gcc not found in PATH." >&2; exit 1; }
command -v python >/dev/null 2>&1 || { echo "[ERROR] python not found in PATH." >&2; exit 1; }

mkdir -p "$OUTPUT_DIR"

echo "========================================"
echo "Deleting previous artifacts..."
echo "========================================"
rm -f "$OUTPUT_DIR"/* || true

echo "========================================"
echo "Building Redis Clone with SQL Layer..."
echo "========================================"

echo "Compiling server..."
pushd "$BACKEND_DIR" >/dev/null
gcc -O2 -Wall -Wextra dict.c rdb.c ae.c resp.c avl.c zset.c tpool.c sds.c mempool.c expheap.c aofbuf.c server.c -o "$OUTPUT_DIR/server"
popd >/dev/null
echo "[SUCCESS] Server compiled to $OUTPUT_DIR/server"

echo "Compiling SQL Processor shared object..."
gcc -shared -fPIC -O2 -Wall -Wextra "$QP_DIR/sql_parser.c" "$QP_DIR/sql_lexer.c" "$QP_DIR/sql_parser_internal.c" "$QP_DIR/sql_translator.c" "$QP_DIR/schema_manager.c" "$QP_DIR/py_interface.c" "$BACKEND_DIR/sds.c" -o "$OUTPUT_DIR/sql_processor.so"
echo "[SUCCESS] SQL Processor compiled to $OUTPUT_DIR/sql_processor.so"

echo "Compiling Query Processor Server..."
gcc -O2 -Wall -Wextra "$QP_DIR/sql_parser.c" "$QP_DIR/sql_lexer.c" "$QP_DIR/sql_parser_internal.c" "$QP_DIR/sql_translator.c" "$QP_DIR/schema_manager.c" "$QP_DIR/qp_server.c" "$BACKEND_DIR/sds.c" -o "$OUTPUT_DIR/qp_server"
echo "[SUCCESS] Query Processor Server compiled to $OUTPUT_DIR/qp_server"

echo "Compiling SQL CLI..."
gcc -O2 -Wall -Wextra "$QP_DIR/sql_parser.c" "$QP_DIR/sql_lexer.c" "$QP_DIR/sql_parser_internal.c" "$QP_DIR/sql_translator.c" "$QP_DIR/schema_manager.c" "$UI_DIR/table_formatter.c" "$UI_DIR/cli.c" "$BACKEND_DIR/sds.c" -o "$OUTPUT_DIR/sql_cli"
echo "[SUCCESS] SQL CLI compiled to $OUTPUT_DIR/sql_cli"

echo "========================================"
echo "Build Complete."
echo "========================================"

exit 0
