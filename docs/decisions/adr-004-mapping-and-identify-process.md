# ADR-004: Identificacion y Mapeo Interno de Procesos (JobID vs System PID)

## Estado
Aceptado

## Problematica y Contexto
Cada proceso creado por el sistema operativo recibe un identificador propio (`PID`)[cite: 2]. Sin embargo, para el usuario de la CLI resulta incomodo e impractico gestionar tareas mediante numeros de `PID` grandes o impredecibles[cite: 2]. Se debia decidir como identificar las tareas dentro del sistema.

## Factores influyentes
* Usabilidad del CLI (facilidad para cancelar o listar trabajos mediante numeros pequeños como `1`, `2`, `3`)[cite: 2].
* Necesidad de guardar un historial persistente en la base de datos (SQLite)[cite: 2].
* Mantener el control de los procesos en segundo plano usando las APIs de POSIX (`kill`, `waitpid`)[cite: 2].

## Opciones consideradas
* **Usar unicamente el `PID` del sistema operativo como identificador general.**
* **Mapear un `JobID` autoincremental de la base de datos contra el `PID` del proceso en memoria**[cite: 2].

## Decision tomada
Se decidio **desacoplar el identificador de usuario (`JobID`) del `PID` del kernel**[cite: 2], administrando la relacion entre ambos a traves de un mapa global en memoria (`std::map`)[cite: 2].

### Consecuencias positivas
* **Mejor experiencia de usuario:** La CLI opera con identificadores simples y secuenciales (`JobID`)[cite: 2].
* **Persistencia:** El `JobID` trasciende el ciclo de vida del proceso y permanece registrado en la tabla de SQLite[cite: 2].
* **Separación de responsabilidades:** Las llamadas del sistema usan el `PID` real mientras la interfaz y la BD usan el `JobID`[cite: 2].

### Consecuencias negativas
* Requiere mantener sincronizados el `vector` y el `mapaIdPid` en memoria[cite: 2], debiendo limpiar ambas estructuras al finalizar un proceso[cite: 2].

## Ventajas y Desventajas de las opciones

### Opcion 1: Uso directo de `PID`
* **Ventajas:** No requiere estructuras de mapeo adicionales en memoria.
* **Desventajas:** Mala experiencia de usuario (numeros largos e impredecibles) y perdida de abstraccion con los registros de la base de datos[cite: 2].

### Opcion 2: Mapeo `JobID` <-> `PID`
* **Ventajas:** Interfaz limpia para el usuario[cite: 2], compatibilidad directa con llaves primarias de SQLite y desacoplamiento de la capa de sistema operativo[cite: 2].
* **Desventajas:** Ligero costo en memoria por mantener el mapa de traduccion en tiempo de ejecucion[cite: 2].