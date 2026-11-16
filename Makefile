
.PHONY: all install-deps configure build test clean

all: install-deps configure build

install-deps:
	@echo "=== Instalando dependencias de JUAN ==="
	@sudo apt-get update
	@sudo apt-get install -y build-essential cmake libncurses-dev libtinfo-dev

configure:
	@echo "=== Configurando JUAN ==="
	@cmake -S . -B build

build:
	@echo "=== Compilando JUAN ==="
	@cmake --build build -j$$(nproc)

test:
	@echo "=== Ejecutando pruebas ==="
	@ctest --test-dir build --output-on-failure

clean:
	@cmake -E rm -rf build
