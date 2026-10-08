# ADR-003: Seleccion de Mecanismo de Supervision y Recopilacion de Procesos Relativos a Procesos Hijo
## Estado
Aceptado
## Problemática y Contexto
Se necesita que el CLI pueda monitorear la ejecucion de los jobs lanzados en segundo plano para actualizar su estado (SUCCEDED, FAILED, CANCELING) y liberar slots en la lista de trabajos sin congelar la terminal del usuario. Se debia elegir el mecanismo POSIX adecuado para consultar procesos hijo finalizados.
## Factores influyentes
* Evitar que la interfaz del CLI se bloquee mientras se ejecutan comandos largos.
* Garantizar la seguridad al manipular estructuras de datos en C++.
* Mantener la portabilidad dentro del estandar POSIX.
## Opciones consideradas
* **Consulta periodica no bloqueante (waitpid con WNOHANG).**
* **Llamadas especificas de BSD/Linux(wait3/wait4).**
* **Manejador asincronico de señales (SIGCHLD)**
## Decisión tomada
Se eligio tomar waitpid con el flag WNOHANG dentro de una funcion de actualizacion periodica(actualizarJobs()) invocada en el bucle principal de la aplicacion. Se descarta en el uso de wait3 y wait4.
### Consecuencias positivas
* **Simplicidad:** Mantiene el monitoreo dentro del flujo secuencial de la aplicacion son complejidad de hilos o interrupciones
* **Seguridad de memoria:** Al no usar *signal handles*, se evita la corrupcion de datos al modificar estructuras de C++.
* **Estandar:** Es una llamada nativa POSIX sin dependencias de extensiones historicas de BSD
### Consecuencias negativas
* El estado de los trabajos solo se actualiza cuando el bucle principal ejecuta actualizarJobs(), generando un leve retraso respecto a un enfoque por eventos inmediatos

## Ventajas y Desventajas de las opciones
### waitpid con WNOHANG
* **Ventajas:** No bloquear la ejecucion principal, es facil de implepmentar con POSIX
* **Desventajas** Requiere sondeo (*polling*) en cada iteracion de bucle principal

### wait3/wait4
* **Ventajas:** Permite obtener metricas avanzadas de uso de recursos del sistema(struct rusage)
* **Desventajas:** Innecesario para el alcance del proyecto y menos portable al ser llamadas heredadas de BSD

### Manejador de señal SIGCHLD
* **Ventajas:** Notificacion inmediata en el momento exacto enel que muere un proceso hijo.
* **Desventajas:** Propenso a errores de concurrencia y restricciones severas de funcioneas seguras (*async-signal-safe*) dentro del *handle*