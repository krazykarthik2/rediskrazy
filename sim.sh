#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXEC_DIR="$SCRIPT_DIR/execs"

echo "===================================================="
echo "STEP 0: Checking prerequisites and cleaning up..."
echo "===================================================="

command -v gcc >/dev/null 2>&1 || { echo "[ERROR] gcc not found in PATH." >&2; exit 1; }
command -v python >/dev/null 2>&1 || { echo "[ERROR] python not found in PATH." >&2; exit 1; }

echo "Cleaning up any leftover server processes..."
pkill -f "$EXEC_DIR/server" >/dev/null 2>&1 || true
pkill -f "$EXEC_DIR/qp_server" >/dev/null 2>&1 || true

echo ""
echo "===================================================="
echo "STEP 1: Building Project..."
echo "===================================================="
"$SCRIPT_DIR/build.sh"

echo ""
echo "===================================================="
echo "STEP 2: Running Automated Tests..."
echo "===================================================="
sleep 1
python test.py

echo ""
echo "===================================================="
echo "STEP 3: Starting Redis Server for CLI..."
echo "===================================================="

"$EXEC_DIR/server" > server.log 2>&1 &
SERVER_PID=$!
"$EXEC_DIR/qp_server" > qp_server.log 2>&1 &
QP_PID=$!
sleep 3

echo ""
echo "===================================================="
echo "STEP 4: Choose Command Line Interface (CLI)"
echo "===================================================="
echo "[1] Start C-based SQL CLI (sql_cli)"
echo "[2] Start Python-based CLI Wrapper (librediskrazy)"
echo "===================================================="
echo ""

read -r -p "Enter choice (1 or 2, default is 1): " choice || true
choice="${choice:-1}"
choice="$(echo "$choice" | tr -d '[:space:]')"

if [ "$choice" = "2" ]; then
    echo ""
    echo "Starting Python CLI Wrapper..."
    python -m librediskrazy.client
else
    echo ""
    echo "Starting SQL CLI Layer..."
    "$EXEC_DIR/sql_cli"
fi

echo ""
echo "Cleaning up..."
kill "$SERVER_PID" >/dev/null 2>&1 || true
kill "$QP_PID" >/dev/null 2>&1 || true

exit 0
