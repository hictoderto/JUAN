#!/bin/bash
set -e

echo "=== JUAN Overload Verification ==="
echo "Objetivo: intentar ejecutar 11 jobs simultáneos."
echo "Límite esperado: 10 jobs."
echo

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
JUAN="$BUILD_DIR/juan"

echo "[1] Compilando..."

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"

echo
echo "[2] Verificando ejecutable..."

if [ ! -f "$JUAN" ]; then
    echo "ERROR: no se encontró $JUAN"
    exit 1
fi

echo "OK: ejecutable encontrado."
echo

echo "[3] Enviando 11 procesos sleep 30..."
echo

{
    for i in {1..11}; do
        echo "correr sleep 30"
    done

    echo "jobs"
    echo "salir"
} | "$JUAN"

echo
echo "=== Overload verification complete ==="