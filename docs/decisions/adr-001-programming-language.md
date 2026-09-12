# ADR-001: Lenguaje de Programación
## Estado
Aceptado
## Problemática y Contexto
Se debe decir el lenguaje de programación para **JUAN**. Se tienen los siguientes requisitos:
-  Debe ser capaz de realizar llamadas del sistema de Linux de forma práctica y eficiente.
-  Debe permitirnos crear subprocesos, pipes y sockets de manera nativa.
-  Debe permitirnos usar varios niveles de abstracción, para facilitar la escritura y mantenimiento del código.
## Factores influyentes
-  Los miembros del equipo han trabajado principalmente con Python, C y C++.
## Opciones consideradas
- Python
- C
- C++
## Decisión tomada
Se eligió C++ como lenguaje a usar para **JUAN** ya que cumple con todos los requisitos. Permite acceso a todas las funciones estándar de Linux, que son accesibles desde C, mientras que permite manejar niveles de abstracción más altos para facilitar el desarrollo. Además, tiene una extensa librería estándar que el equipo puede aprovechar.

### Consecuencias positivas
- Se espera que la familiaridad de los miembros del equipo con el lenguaje permita un desarrollo más ágil del proyecto.
- Facilita cumplir con el RNF-05 por la eficiencia esperable del lenguaje.
- Al ser un lenguaje compilado se espera poder encontrar una gran cantidad de errores posibles antes de ejecutar el programa.
### Consecuencias negativas
- Posible mayor complejidad de desarrollo comparado con lenguajes de más alto nivel.
- Manejo de dependencias más complicado.

## Ventajas y Desventajas de las opciones
### Python
- ventaja: Permite abstracciones de alto nivel.
- ventaja: Librería estándar extensa.
- desventaja: Menor eficiencia.
- desventaja: Difícil acceso a funciones nativas de Linux.
- desventaja: Fácil manejo de dependencias.
### C
- ventaja: Permite fácil acceso a funciones nativas de Linux.
- ventaja: Máxima eficiencia.
- desventaja: Librería estándar limitada.
- desventaja: Difícil uso de abstracciones de más alto nivel.
- desventaja: Manejo de dependencias complicado.

### C++
- ventaja: Permite fácil acceso a funciones nativas de linux.
- ventaja: Eficiencia comparable a C.
- ventaja: Librería estándar extensa.
- ventaja: Permite abstracciones de más alto nivel.
- desventaja: Mayor complejidad comparado con otros lenguajes.
- desventaja: Manejo de dependencias complicado.
