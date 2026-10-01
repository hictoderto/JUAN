#!/bin/bash

set -e

echo "=== JUAN Verification ==="

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
JUAN="$BUILD_DIR/juan"

echo "[1] Compilando..."

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"

echo "[2] Verificando ejecutable..."

if [ ! -f "$JUAN" ]; then
    echo "ERROR: no se encontró $JUAN"
    exit 1
fi

echo "OK: ejecutable encontrado."

echo "[3] Ejecutando prueba:"
echo "    correr sleep 100"
echo "    salir"

printf "correr sleep 100\nsalir\n" | "$JUAN"

echo "=== Verification  creacion de trabajo completa ==="