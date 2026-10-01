#!/bin/bash

set -e

echo "=== JUAN Verification ==="

# Directorio raíz del proyecto
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

BUILD_DIR="$PROJECT_ROOT/build"

echo "[1] Compilando..."

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"

echo "[2] Verificando ejecutable..."

if [ -f "$BUILD_DIR/juan" ]; then
    echo "OK: ejecutable encontrado."
else
    echo "ERROR: no se encontró el ejecutable."
    exit 1
fi

echo "[3] Ejecutando pruebas..."

"$BUILD_DIR/juan"

echo "=== Verification complete ==="