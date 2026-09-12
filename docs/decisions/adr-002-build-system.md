# ADR-001: Build System
## Estado
Aceptado
## Problemática y Contexto
El desarrollo del Job Runner en C++ requiere un sistema de construcción (build system) eficiente, mantenible y escalable. Se necesita automatizar la compilación de múltiples archivos fuente, la gestión de banderas del compilador, la vinculación de bibliotecas de concurrencia (`pthread`) y la integración futura de suites de pruebas unitarias sin depender de scripts manuales o configuraciones ligadas a un solo entorno.
## Factores influyentes
* **Multiplataforma:** La necesidad de compilar el proyecto de forma consistente en distintos entornos de desarrollo y distribuciones Linux.
* **Mantenibilidad y Escalabilidad:** Simplicidad para agregar nuevos módulos, archivos de cabecera (`.hpp`) y fuentes (`.cpp`) conforme el proyecto crezca hacia los siguientes hitos.
* **Integración con IDEs y herramientas:** Compatibilidad nativa con editores y herramientas de análisis estático/dinámico (Valgrind, Clang-Tidy, VS Code, CLion).
* **Integración con C++20:** Facilidad para configurar estándares modernos de C++ y banderas del compilador (`-Wall`, `-Wextra`, `-pthread`).
## Opciones consideradas
* Makefile tradicional (GNU Make)
* CMake
## Decisión tomada
Se elige **CMake** como el generador de archivos de construcción principal del proyecto.
### Consecuencias positivas
* **Abstracción del entorno:** Genera automáticamente los Makefiles o archivos de construcción según el sistema operativo y compilador detectado.
* **Sintaxis declarativa limpia:** Permite definir objetivos (`add_executable`), bibliotecas y dependencias de forma modular sin escribir reglas complejas de compilación a mano.
* **Gestión sencilla de librerías:** Facilita la vinculación de librerías de hilos (`find_package(Threads REQUIRED)`) y futuras dependencias externas para pruebas o persistencia.
* **Soporte estándar en la industria:** Simplifica el trabajo colaborativo en Git, ya que solo se rastrea el archivo `CMakeLists.txt`.
### Consecuencias negativas
* **Curva de aprendizaje inicial:** Requiere que todos los integrantes del equipo conozcan la sintaxis básica del lenguaje de CMake.
* **Capa adicional de abstracción:** Añade un paso previo al proceso de compilación (`cmake -B build` seguido de `cmake --build build`).

## Ventajas y Desventajas de las opciones
### Makefile
* **Ventajas:**
  * Control total y directo sobre los comandos que se ejecutan en la terminal.
  * Ampliamente conocido en entornos Linux/Unix.
* **Desventajas:**
  * La sintaxis se vuelve compleja y difícil de mantener cuando se manejan múltiples subdirectorios (`src/`, `include/`, `tests/`).
  * Dependiente del sistema operativo y de la concha de comandos (*shell*) subyacente.
  * Propenso a errores sutiles sintácticos (como el uso estricto de identificadores con tabuladores).

### CMake
* **Ventajas:**
  * Generación automática de dependencias entre archivos fuente y cabeceras.
  * Excelente integración con C++ moderno, suites de testing (CTest/GoogleTest) y herramientas de análisis.
  * Escalable y estándar para proyectos profesionales en C++.
* **Desventajas:**
  * Requiere tener instalado CMake en el sistema de desarrollo.
  * La configuración inicial del `CMakeLists.txt` requiere mayor abstracción que un Makefile trivial.