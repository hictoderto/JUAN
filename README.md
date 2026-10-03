# JUAN — Job Utility And Network-runner

>  **Proyecto en proceso de desarrollo**

JUAN (**Job Utility And Network-runner**) es un proyecto desarrollado para la materia **Programación de Sistemas Avanzados 2026B**.

El proyecto tiene como objetivo desarrollar un sistema para **recibir, administrar, ejecutar y monitorear trabajos**, permitiendo su operación local y, posteriormente, mediante una red privada.

Actualmente, JUAN se encuentra en una **etapa de desarrollo activo**. Ya cuenta con una implementación funcional para la ejecución y administración básica de trabajos locales, mientras que otras funcionalidades, pruebas, documentación y componentes del sistema continúan en desarrollo.

---

## Integrantes

* **Adrián Arturo García López**
* **Nicolás Tadeo Cáceres Pánuco**
* **Lara Romero Ramiro**

---

## Estado del proyecto

**Estado:** En desarrollo

Actualmente JUAN cuenta con una primera implementación funcional que permite:

* Crear y ejecutar trabajos como procesos independientes.
* Asignar un identificador único a cada trabajo.
* Asociar el identificador del trabajo con el PID del proceso Linux.
* Consultar los trabajos actualmente en ejecución.
* Consultar el estado general del sistema.
* Cancelar trabajos mediante su identificador.
* Detectar automáticamente cuando un proceso termina.
* Obtener el código de salida de un proceso.
* Diferenciar entre trabajos completados correctamente, fallidos y cancelados.
* Mantener información básica de los trabajos mediante SQLite.
* Mantener el servicio activo después de recibir comandos inválidos.

La implementación continúa en desarrollo y posteriormente se incorporarán mejoras relacionadas con persistencia, pruebas, manejo de errores, documentación, comunicación mediante red y otras funcionalidades definidas en los requisitos del proyecto.

---

# Requisitos

Para compilar y ejecutar la versión actual de JUAN se necesita un sistema Linux y contar con las siguientes herramientas:

* **CMake**
* **Compilador de C++ 23+**
* **Git**
* **Conexión a Internet durante la configuración inicial**, necesaria para obtener las dependencias del proyecto, incluyendo SQLite mediante el sistema de construcción utilizado.

### Dependencias principales

| Dependencia    | Uso                                           |
| -------------- | --------------------------------------------- |
| CMake          | Configuración y construcción del proyecto     |
| Compilador C++ | Compilación del código fuente                 |
| SQLite         | Persistencia de información de los trabajos   |
| POSIX/Linux    | Procesos, `fork`, `exec`, señales y `waitpid` |

> **Nota:** JUAN utiliza funcionalidades específicas de sistemas Linux/POSIX, por lo que la versión actual está destinada a ejecutarse en Linux.

---

# Instalación

## 1. Clonar el repositorio

Clona el repositorio:

```bash
git clone https://github.com/hictoderto/JUAN.git
```

Entra al directorio del proyecto:

```bash
cd JUAN
```

Para trabajar específicamente con la rama de desarrollo actual:

```bash
git checkout commands
```

---

## 2. Verificar las herramientas necesarias

Comprueba que CMake esté instalado:

```bash
cmake --version
```

Comprueba que exista un compilador de C++:

```bash
g++ --version
```

También puede utilizarse otro compilador compatible con C++.

Verifica Git:

```bash
git --version
```

Si alguna de estas herramientas no está instalada, debe instalarse antes de continuar.

---

# Compilación

La compilación se realiza utilizando CMake.

Desde la raíz del repositorio:

```bash
cmake -S . -B build
```

Posteriormente:

```bash
cmake --build build
```

Si la compilación termina correctamente, se generará el ejecutable de JUAN dentro del directorio:

```text
build/juan
```

---

# Ejecución

Para iniciar JUAN:

```bash
./build/juan
```

Al ejecutarse correctamente se mostrará algo similar a:

```text
JUAN iniciado. PID: 12345
JUAN>
```

JUAN permanecerá ejecutándose y esperará comandos desde la terminal.

---

# Uso

JUAN utiliza una interfaz interactiva basada en comandos.

Los comandos disponibles actualmente son:

| Comando            | Descripción                                                         |
| ------------------ | ------------------------------------------------------------------- |
| `ayuda`            | Muestra los comandos disponibles                                    |
| `help`             | Alias de `ayuda`                                                    |
| `status`           | Muestra el estado general de JUAN y la cantidad de trabajos activos |
| `correr <comando>` | Ejecuta un comando de Linux como un nuevo trabajo                   |
| `jobs`             | Muestra los trabajos actualmente en ejecución                       |
| `cancelar <id>`    | Solicita la cancelación de un trabajo mediante su ID                |
| `salir`            | Finaliza JUAN después de esperar a los trabajos activos             |
| `exit`             | Alias de `salir`                                                    |

---

## Mostrar ayuda

Para consultar los comandos disponibles:

```text
JUAN> ayuda
```

Ejemplo:

```text
Comandos disponibles:
  ayuda   - Mostrar ayuda
  status  - Mostrar estados
  correr  - Ejecutar comando Linux
  cancelar <id>  - cancela un proceso en ejecucion
  jobs    - Mostrar procesos
  salir   - Cerrar JUAN
```

---

# Ejecutar un trabajo

Para ejecutar un comando de Linux se utiliza:

```text
correr <comando>
```

Por ejemplo:

```text
JUAN> correr sleep 30
```

JUAN crea un proceso independiente y asigna un identificador al trabajo:

```text
Job creado.
PID: 15234
job id: 1
Jobs activos: 1/10
```

En este ejemplo:

* `1` es el **ID del Job asignado por JUAN**.
* `15234` es el **PID asignado por Linux**.
* `sleep 30` es el comando solicitado.

El ID del Job y el PID son identificadores diferentes.

---

# Consultar trabajos

Para consultar los trabajos actualmente activos:

```text
JUAN> jobs
```

Ejemplo:

```text
Jobs activos:

ID: 1 | PID: 15234 | Comando: sleep 30 | Estado: 2
ID: 2 | PID: 15235 | Comando: sleep 60 | Estado: 2
```

El estado corresponde al valor definido en `job.h`.

Actualmente se utilizan los siguientes estados:

```text
QUEUED
LAUNCHED
RUNNING
SUCCEDED
REJECTED
CANCELING
CANCELED
FAILED
```

---

# Consultar el estado de JUAN

Para consultar el estado general del sistema:

```text
JUAN> status
```

Ejemplo:

```text
JUAN funcionando...
Jobs activos: 2/10
```

La implementación actual permite un máximo de:

```text
10 jobs activos
```

---

# Cancelar un trabajo

Para cancelar un trabajo se utiliza su identificador:

```text
cancelar <id>
```

Por ejemplo, si existe:

```text
ID: 1 | PID: 15234 | Comando: sleep 100
```

se puede solicitar su cancelación mediante:

```text
JUAN> cancelar 1
```

JUAN obtiene el PID asociado al ID mediante su mapa interno y envía una señal `SIGTERM` al proceso.

El estado del trabajo pasa de:

```text
RUNNING
```

a:

```text
CANCELING
```

Cuando JUAN detecta que el proceso terminó, el trabajo pasa a:

```text
CANCELED
```

Ejemplo:

```text
JUAN> cancelar 1
Cancelando job 1 (PID 15234)...

[JOB TERMINADO]
ID: 1
PID: 15234
Comando: sleep 100
Terminado por senal: 15
```

---

# Detección de trabajos terminados

JUAN utiliza `waitpid()` con la opción:

```cpp
WNOHANG
```

Esto permite consultar si un proceso terminó sin bloquear la ejecución principal del programa.

El sistema revisa continuamente los procesos activos y detecta cuando un trabajo termina.

Cuando un proceso finaliza correctamente, JUAN obtiene su código de salida.

Por ejemplo:

```text
[JOB TERMINADO]
ID: 3
PID: 15421
Comando: echo hola
Codigo: 0
```

Un código:

```text
0
```

indica que el proceso terminó correctamente.

Un código diferente de `0` indica que el proceso terminó con un error.

---

# Ejecución de comandos

Los trabajos se ejecutan mediante un proceso hijo creado utilizando:

```cpp
fork()
```

El proceso hijo ejecuta posteriormente el comando mediante:

```cpp
execl("/bin/sh", "sh", "-c", comando.c_str(), nullptr);
```

Esto permite que JUAN pueda ejecutar comandos de Linux como:

```text
correr ls
correr pwd
correr echo Hola
correr sleep 10
```

La salida estándar y la salida de error de los trabajos se redirigen actualmente al archivo:

```text
job_output.log
```

Por ejemplo:

```text
correr echo Hola
```

generará la salida correspondiente en:

```text
job_output.log
```

---

# Base de datos

JUAN utiliza **SQLite** para almacenar información relacionada con los trabajos.

Durante la ejecución se crea el archivo:

```text
data.db
```

La tabla utilizada actualmente es:

```sql
jobs
```

y contiene información como:

* ID del trabajo.
* Comando.
* Estado.
* Tiempo de espera.
* Tiempo de lanzamiento.
* Tiempo de finalización.
* Resultado.

La estructura actual es:

```text
jobs
├── id
├── command
├── status
├── queued_at
├── launched_at
├── finished_at
└── result
```

---

# Arquitectura básica

La implementación actual utiliza varios componentes de Linux y C++ para administrar los trabajos.

```text
                    JUAN
                     │
                     ▼
              ┌─────────────┐
              │   Comando   │
              │  del usuario│
              └──────┬──────┘
                     │
                     ▼
              ┌─────────────┐
              │     Job     │
              │             │
              │ JobID       │
              │ command     │
              │ status      │
              │ result      │
              └──────┬──────┘
                     │
                     ▼
                 fork()
                     │
              ┌──────┴──────┐
              │             │
              ▼             ▼
          JUAN padre     Proceso hijo
              │             │
              │             ▼
              │          exec()
              │             │
              │             ▼
              │        Comando Linux
              │
              ▼
         mapaIdPid
              │
              │ JobID → PID
              │
              ▼
           waitpid()
              │
              ▼
       Estado / resultado
```

---

# Estructura del repositorio

La estructura definida para el proyecto es:

```text
JUAN/
│
├── README.md
│
├── src/
│   └── Código de producción
│
├── inc/
│   └── Headers del proyecto
│
├── docs/
│   ├── user-guide/
│   │   └── Guía de instalación y operación
│   │
│   ├── technical-guide/
│   │   └── Documentación técnica
│   │
│   ├── decisions/
│   │   └── Architecture Decision Records (ADR)
│   │
│   ├── ai-usage/
│   │   └── Registro del uso de herramientas de IA
│   │
│   ├── change-requests/
│   │   └── Solicitudes de cambio
│   │
│   └── incidents/
│       └── Registro de incidentes
│
├── verif/
│   ├── verification-plan/
│   │   └── Plan y matriz de trazabilidad
│   │
│   ├── test-cases/
│   │   └── Casos de prueba TC-XXX
│   │
│   ├── scripts/
│   │   └── Automatización de pruebas
│   │
│   ├── test-data/
│   │   └── Datos controlados
│   │
│   └── results/
│       └── Evidencia de ejecuciones
│
├── project-management/
│   └── Planificación, roles, minutas y acuerdos
│
└── .github/
    └── ISSUE_TEMPLATE/
        └── Plantillas de Issues
```

---

# Limitaciones actuales

La versión actual todavía se encuentra en desarrollo.

Entre las principales limitaciones se encuentran:

* La operación remota todavía no está implementada.
* La cancelación utiliza actualmente señales POSIX.
* La persistencia de los estados de los trabajos continúa en desarrollo.
* El historial completo de trabajos todavía requiere integración adicional con SQLite.
* Las marcas de tiempo de los trabajos todavía no están completamente implementadas.
* El manejo de grupos de procesos todavía puede mejorarse para comandos que generan procesos hijos.
* La interfaz de usuario actualmente es una consola básica.
* Las pruebas automatizadas continúan en desarrollo.
* La documentación técnica continúa en construcción.

Estas limitaciones serán atendidas conforme avance el proyecto.

---

# Pruebas y verificación

El proyecto cuenta con un directorio destinado a las pruebas y verificaciones:

```text
verif/
```

Las pruebas automatizadas se encuentran en:

```text
verif/scripts/
```

Para realizar una compilación y verificación del proyecto puede utilizarse el script correspondiente cuando esté disponible:

```bash
./verif/scripts/verif_runs.sh
```

Los resultados y evidencias de las pruebas se almacenan en:

```text
verif/results/
```

---

# Desarrollo

Para contribuir al desarrollo del proyecto se recomienda crear una rama para cada funcionalidad o cambio:

```bash
git checkout -b nombre-de-la-funcionalidad
```

Después de realizar los cambios:

```bash
git add .
git commit -m "Descripción del cambio"
git push origin nombre-de-la-funcionalidad
```

La rama actual de desarrollo de comandos es:

```text
commands
```

---

# Tecnologías utilizadas

* **C++**
* **CMake**
* **SQLite**
* **Linux / POSIX**
* **Git / GitHub**

Entre las funcionalidades POSIX utilizadas actualmente se encuentran:

* `fork()`
* `execl()`
* `waitpid()`
* `kill()`
* `SIGTERM`
* `read()`
* `fcntl()`
* `O_NONBLOCK`

---

# Objetivo del proyecto

JUAN busca evolucionar desde una implementación local de ejecución y administración de trabajos hacia un sistema capaz de:

1. Recibir trabajos.
2. Asignar identificadores únicos.
3. Administrar estados.
4. Ejecutar trabajos de forma independiente.
5. Consultar su estado.
6. Cancelarlos.
7. Registrar sus resultados.
8. Mantener información persistente.
9. Automatizar su verificación.
10. Permitir posteriormente la operación mediante una red privada.

>  **El proyecto continúa en desarrollo. La documentación y las funcionalidades se actualizarán conforme avance la implementación.**

