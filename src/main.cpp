#include "database.h"
#include "job.h"
#include "result.h"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <juan_session.h>
#include <logger.h>
#include <map>
#include <netinet/in.h>
#include <queue>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <sqlite3.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <types.h>
#include <unistd.h>
#include <unordered_map>

#include <event_loop.h>

#ifndef P_PIDFD
#define P_PIDFD 3
#endif

using namespace juan;
using namespace std;

using Reloj = std::chrono::steady_clock;

map<JobID, DeadlineEventHandle> temporizadoresCancelacion;
unordered_map<int, FDEventHandle> handlesEventos;
unordered_map<int, ClienteTCP> clientes;
queue<Job> trabajosEnCola;
vector<Job> RuningJobs;
map<JobID, pid_t> mapaIdPid;
queue<string> comandosTerminal;
// Relacionar cada job con su pidfd.
map<JobID, int> mapaIdPidfd;
// Relacionar cada descriptor pidfd con su job.
unordered_map<int, JobID> mapaFdJob;

bool finEntradaTerminal = false;
bool procesandoComandoTerminal = false;
const int TIMEOUT_CANCELACION_MS = 5000;
const int MAX_JOBS = 5;
const int MAX_COLA = 5;

void mostrarMensajeRed(const string& mensaje);
void procesarTerminacionPidfd(int pidfd, std::shared_ptr<Logger> logger, EventLoop& loop);

// Abrir un descriptor asociado a un proceso.
int abrirPidfd(pid_t pid) {
    return static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
}

// Enviar señales utilizando el descriptor del proceso.
int enviarSenalPidfd(int pidfd, int senal) {
    return static_cast<int>(syscall(SYS_pidfd_send_signal, pidfd, senal, nullptr, 0));
}
bool programarCancelacion(JobID id, auto logger, EventLoop& loop) {

    auto plazo = Reloj::now() + std::chrono::milliseconds(TIMEOUT_CANCELACION_MS);

    auto resultado = loop.add_deadline_event(plazo, [id, logger]() {
        // El deadline ya se está ejecutando.
        // Su iterador deja de ser válido.
        temporizadoresCancelacion.erase(id);

        auto it = mapaIdPidfd.find(id);

        // Si el proceso ya terminó, no hacer nada.
        if (it == mapaIdPidfd.end()) {
            return;
        }

        int pidfd = it->second;

        if (enviarSenalPidfd(pidfd, SIGKILL) == 0) {

            mostrarMensajeRed("[TIMEOUT] Job " + to_string(id) + ": se envio SIGKILL.");

        } else if (errno != ESRCH) {

            logger->error("Error enviando SIGKILL al job {}: {}", id, strerror(errno));
        }
    });

    if (!resultado) {
        logger->error("Error programando timeout para job {}", id);
        return false;
    }

    temporizadoresCancelacion.emplace(id, *resultado);

    return true;
}
// Readline entrega las líneas completas a esta función.
void recibirLinea(char* texto) {
    if (texto == nullptr) {
        finEntradaTerminal = true;
        return;
    }

    string comando(texto);

    if (!comando.empty()) {
        add_history(texto);
        comandosTerminal.push(comando);
    }

    free(texto);
}

int crearServidor(int puerto, auto logger) {

    int servidor = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (servidor == -1) {
        logger->error("Error creando socket");
        return -1;
    }

    int opt = 1;

    if (setsockopt(servidor, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {

        logger->error("setsockopt");
        close(servidor);
        return -1;
    }

    sockaddr_in direccion{};

    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port = htons(puerto);

    if (bind(servidor, reinterpret_cast<sockaddr*>(&direccion), sizeof(direccion)) == -1) {

        logger->error("Error en bind");
        close(servidor);
        return -1;
    }

    if (listen(servidor, SOMAXCONN) == -1) {

        logger->error("Error en listen");
        close(servidor);
        return -1;
    }

    return servidor;
}

bool cancelarEnCola(JobID id, sqlite3* dbConn, auto logger) {
    queue<Job> auxiliar;
    bool encontrado = false;

    while (!trabajosEnCola.empty()) {
        Job actual = trabajosEnCola.front();
        trabajosEnCola.pop();

        if (actual.id == id) {
            encontrado = true;
            continue; // No lo copiamos: queda eliminado.
        }

        auxiliar.push(actual);
    }

    trabajosEnCola = std::move(auxiliar);

    if (!encontrado) {
        return false;
    }

    // Actualizar el estado del job cancelado en SQLite.
    sqlite3_stmt* stmt = nullptr;

    const char* sql = "UPDATE jobs SET status = ? WHERE id = ?";

    if (sqlite3_prepare_v2(dbConn, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        logger->error("Error preparando actualizacion: {}", sqlite3_errmsg(dbConn));
        return true; // Se eliminó de la cola, aunque falló SQLite.
    }

    sqlite3_bind_int(stmt, 1, CANCELED);
    sqlite3_bind_int64(stmt, 2, id);

    if (sqlite3_step(stmt) != SQLITE_DONE) {

        logger->error("Error actualizando job:: {}", sqlite3_errmsg(dbConn));
    }

    sqlite3_finalize(stmt);

    return true;
}

void correr(Job nuevoJob, auto logger, EventLoop& loop) {
    pid_t child = fork();

    if (child == -1) {
        logger->error("Error al crear el proceso: {}", strerror(errno));
        return;
    }

    if (child == 0) {
        string logFileNameOut = "logs/job_output/" + to_string(nuevoJob.id) + ".log";

        string logFileNameErr = "logs/job_error/" + to_string(nuevoJob.id) + ".log";

        int salida_out = open(logFileNameOut.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

        int salida_err = open(logFileNameErr.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (salida_out == -1 || salida_err == -1) {
            if (salida_out != -1)
                close(salida_out);
            if (salida_err != -1)
                close(salida_err);
            _exit(1);
        }

        dup2(salida_out, STDOUT_FILENO);
        dup2(salida_err, STDERR_FILENO);

        close(salida_out);
        close(salida_err);

        execl("/bin/sh", "sh", "-c", nuevoJob.command.c_str(), nullptr);

        _exit(127);
    }

    // Proceso padre: abrir el pidfd.
    int pidfd = abrirPidfd(child);

    if (pidfd == -1) {
        logger->error("Error abriendo pidfd para PID {}: {}", child, strerror(errno));

        // Aquí todavía no existe un pidfd válido.
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        return;
    }

    // Registrar pidfd en EventLoop.
    auto eventoPidfd =
        loop.add_fd_event(pidfd, EPOLLIN, [pidfd, logger, &loop](FDEventFlags eventos) {
            if (eventos & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                procesarTerminacionPidfd(pidfd, logger, loop);
            }
        });

    if (!eventoPidfd) {
        logger->error("Error registrando PIDFD en EventLoop: {}", strerror(errno));

        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        close(pidfd);
        return;
    }
    // Guardar el identificador del evento
    handlesEventos[pidfd] = *eventoPidfd;

    // Registrar el proceso únicamente después de agregarlo a epoll.
    RuningJobs.push_back(nuevoJob);

    mapaIdPid[nuevoJob.id] = child;
    mapaIdPidfd[nuevoJob.id] = pidfd;
    mapaFdJob[pidfd] = nuevoJob.id;

    logger->info("Job creado. ID: {} | PID: {} | PIDFD: {} | Jobs activos: {}/{}", nuevoJob.id,
                 child, pidfd, RuningJobs.size(), MAX_JOBS);
}
void procesarTerminacionPidfd(int pidfd, std::shared_ptr<Logger> logger, EventLoop& loop) {
    auto fdIt = mapaFdJob.find(pidfd);

    if (fdIt == mapaFdJob.end()) {
        return;
    }

    JobID id = fdIt->second;

    auto jobIt = find_if(RuningJobs.begin(), RuningJobs.end(),
                         [id](const Job& job) { return job.id == id; });

    if (jobIt == RuningJobs.end()) {
        return;
    }

    // Recuperar el estado del proceso mediante pidfd.
    siginfo_t info{};

    if (waitid(static_cast<idtype_t>(P_PIDFD), static_cast<id_t>(pidfd), &info,
               WEXITED | WNOHANG) == -1) {

        if (errno == EINTR) {
            return;
        }

        logger->error("Error en waitid para job {}: {}", id, strerror(errno));

        // Recuperación excepcional para evitar dejar un hijo zombie.
        auto pidIt = mapaIdPid.find(id);

        if (pidIt != mapaIdPid.end()) {
            waitpid(pidIt->second, nullptr, 0);
        }

        jobIt->status = FAILED;
        jobIt->result = -1;
    } else if (info.si_pid == 0) {
        // Todavía no está listo para recolectarse.
        return;

    } else {

        pid_t pid = info.si_pid;

        mostrarMensajeRed("[JOB TERMINADO] ID: " + to_string(id) + " | PID: " + to_string(pid) +
                          " | Comando: " + jobIt->command);

        if (info.si_code == CLD_EXITED) {

            int codigo = info.si_status;

            mostrarMensajeRed("Codigo: " + to_string(codigo));

            jobIt->result = codigo;
            jobIt->status = (codigo == 0) ? SUCCEDED : FAILED;

        } else if (info.si_code == CLD_KILLED || info.si_code == CLD_DUMPED) {

            int senal = info.si_status;

            mostrarMensajeRed("Terminado por senal: " + to_string(senal));

            jobIt->result = -senal;
            jobIt->status = (jobIt->status == CANCELING) ? CANCELED : FAILED;

        } else {

            jobIt->status = FAILED;
            jobIt->result = -1;
        }
    }

    Job terminado = *jobIt;

    // Retirar el descriptor del EventLoop.
    auto handleIt = handlesEventos.find(pidfd);

    if (handleIt != handlesEventos.end()) {
        loop.remove_fd_event(handleIt->second);
        handlesEventos.erase(handleIt);
    }

    // Cerrar el pidfd.
    close(pidfd);

    mapaFdJob.erase(pidfd);
    mapaIdPidfd.erase(id);
    mapaIdPid.erase(id);

    // Eliminar el timeout si el proceso terminó antes de los 5 segundos.
    auto timeoutIt = temporizadoresCancelacion.find(id);

    if (timeoutIt != temporizadoresCancelacion.end()) {

        auto resultado = loop.remove_deadline_event(timeoutIt->second);

        if (!resultado) {
            logger->error("Error eliminando deadline del job {}", id);
        }

        temporizadoresCancelacion.erase(timeoutIt);
    }

    // Eliminar el job terminado.
    RuningJobs.erase(jobIt);

    logger->info("Job {} finalizado. Activos: {}/{}", terminado.id, RuningJobs.size(), MAX_JOBS);

    // Iniciar el siguiente trabajo de la cola.
    if (!trabajosEnCola.empty()) {

        Job siguiente = trabajosEnCola.front();
        trabajosEnCola.pop();

        siguiente.status = LAUNCHED;

        correr(siguiente, logger, loop);
    }
}

JobID registrarJobRed(sqlite3* dbConn, const string& comando, s8 estado) {

    sqlite3_stmt* stmt = nullptr;

    const char* sql = "INSERT INTO jobs (command, status) VALUES (?, ?)";

    if (sqlite3_prepare_v2(dbConn, sql, -1, &stmt, nullptr) != SQLITE_OK) {

        return -1;
    }

    sqlite3_bind_text(stmt, 1, comando.c_str(), -1, SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 2, estado);

    JobID id = -1;

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        id = static_cast<JobID>(sqlite3_last_insert_rowid(dbConn));
    }

    sqlite3_finalize(stmt);

    return id;
}

string procesarComandoRed(const string& comando, sqlite3* dbConn, auto logger, EventLoop& loop) {

    // ==========================================
    // AYUDA
    // ==========================================
    if (comando == "ayuda" || comando == "help") {

        return "Comandos disponibles:\n"
               "  ayuda          - Mostrar ayuda\n"
               "  status         - Mostrar estados de JUAN\n"
               "  correr <cmd>   - Ejecutar comando Linux\n"
               "  cancelar <id>  - Cancelar un job\n"
               "  corriendo      - Mostrar jobs activos\n"
               "  cola           - Mostrar jobs en cola\n"
               "  jobs           - Mostrar todos los jobs\n"
               "  salir          - Cerrar esta conexion";
    }

    // ==========================================
    // STATUS
    // ==========================================
    if (comando == "status") {

        string respuesta;
        respuesta += "JUAN funcionando...\n";

        respuesta += "Clientes conectados: " + to_string(clientes.size()) + "\n";

        respuesta +=
            "Jobs activos: " + to_string(RuningJobs.size()) + "/" + to_string(MAX_JOBS) + "\n";

        respuesta +=
            "Jobs en cola: " + to_string(trabajosEnCola.size()) + "/" + to_string(MAX_COLA);

        return respuesta;
    }

    // ==========================================
    // CANCELAR
    // ==========================================
    if (comando.rfind("cancelar ", 0) == 0) {

        string idTexto = comando.substr(9);

        if (idTexto.empty()) {
            return "Debes indicar el ID del job.";
        }

        try {

            size_t posicion = 0;
            JobID id = stoll(idTexto, &posicion);

            if (posicion != idTexto.size() || id < 0) {
                return "ID invalido.";
            }

            // Primero comprobar si el job está en cola.
            if (cancelarEnCola(id, dbConn, logger)) {

                return "Job " + to_string(id) + " cancelado y eliminado de la cola.";
            }

            // Si no está en cola, buscarlo entre los activos.
            auto it = mapaIdPid.find(id);

            if (it == mapaIdPid.end()) {

                return "No existe un job con ID " + to_string(id) + ".";
            }

            bool yaCancelando = false;

            for (auto& job : RuningJobs) {

                if (job.id == id) {

                    if (job.status == CANCELING) {
                        yaCancelando = true;
                    } else {
                        job.status = CANCELING;
                    }

                    break;
                }
            }

            if (yaCancelando) {

                return "El job " + to_string(id) + " ya tiene una cancelacion pendiente.";
            }

            if (enviarSenalPidfd(mapaIdPidfd.at(id), SIGTERM) == 0) {

                if (!programarCancelacion(id, logger, loop)) {
                    return "SIGTERM enviado al job " + to_string(id) +
                           ", pero no se pudo programar el timeout.";
                }

                return "Cancelacion solicitada para job " + to_string(id) + ". Timeout: 5000 ms.";
            }

            logger->error("Error al enviar SIGTERM: {}", strerror(errno));

            return "Error al solicitar la cancelacion del job " + to_string(id) + ".";

        } catch (const exception&) {

            return "ID invalido.";
        }
    }

    // ==========================================
    // CORRIENDO
    // ==========================================
    if (comando == "corriendo") {

        if (RuningJobs.empty()) {
            return "No hay jobs ejecutandose.";
        }

        string respuesta = "Jobs activos:\n";

        for (const auto& job : RuningJobs) {

            auto it = mapaIdPid.find(job.id);

            pid_t pid = (it != mapaIdPid.end()) ? it->second : -1;

            respuesta += "ID: " + to_string(job.id) + " | PID: " + to_string(pid) +
                         " | Comando: " + job.command +
                         " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";
        }

        return respuesta;
    }

    // ==========================================
    // COLA
    // ==========================================
    if (comando == "cola") {

        if (trabajosEnCola.empty()) {
            return "No hay Jobs en cola.";
        }

        string respuesta = "Jobs en cola:\n";

        auto auxiliar = trabajosEnCola;

        while (!auxiliar.empty()) {

            const auto& job = auxiliar.front();

            respuesta += "ID: " + to_string(job.id) + " | PID: --" + " | Comando: " + job.command +
                         " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";

            auxiliar.pop();
        }

        return respuesta;
    }

    // ==========================================
    // JOBS (ACTIVOS + EN COLA)
    // ==========================================
    if (comando == "jobs") {

        string respuesta;

        // Jobs activos.
        if (RuningJobs.empty()) {

            respuesta += "No hay jobs ejecutandose.\n";

        } else {

            respuesta += "Jobs activos:\n";

            for (const auto& job : RuningJobs) {

                auto it = mapaIdPid.find(job.id);

                pid_t pid = (it != mapaIdPid.end()) ? it->second : -1;

                respuesta += "ID: " + to_string(job.id) + " | PID: " + to_string(pid) +
                             " | Comando: " + job.command +
                             " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";
            }
        }

        // Jobs en cola.
        if (trabajosEnCola.empty()) {

            respuesta += "\nNo hay Jobs en cola.\n";

        } else {

            respuesta += "\nJobs en cola:\n";

            auto auxiliar = trabajosEnCola;

            while (!auxiliar.empty()) {

                const auto& job = auxiliar.front();

                respuesta += "ID: " + to_string(job.id) + " | PID: --" +
                             " | Comando: " + job.command +
                             " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";

                auxiliar.pop();
            }
        }

        return respuesta;
    }

    // ==========================================
    // CORRER
    // ==========================================
    if (comando == "correr " || comando.rfind("correr ", 0) == 0) {

        string cmd = comando.substr(7);

        if (cmd.empty()) {
            return "Debes escribir un comando despues de 'correr'.";
        }

        // Hay espacio para ejecutar inmediatamente.
        if (RuningJobs.size() < MAX_JOBS) {

            JobID id = registrarJobRed(dbConn, cmd, LAUNCHED);

            if (id == -1) {
                return "No se pudo registrar el job en SQLite.";
            }

            Job nuevoJob{};

            nuevoJob.id = id;
            nuevoJob.command = cmd;
            nuevoJob.status = LAUNCHED;

            correr(nuevoJob, logger, loop);

            return "Job creado.\nID: " + to_string(id);
        }

        // Si no hay espacio, intentar agregarlo a la cola.
        if (trabajosEnCola.size() < MAX_COLA) {

            JobID id = registrarJobRed(dbConn, cmd, QUEUED);

            if (id == -1) {
                return "No se pudo registrar el job en SQLite.";
            }

            Job nuevoJob{};

            nuevoJob.id = id;
            nuevoJob.command = cmd;
            nuevoJob.status = QUEUED;

            trabajosEnCola.push(nuevoJob);

            return "Job creado en cola.\n"
                   "ID: " +
                   to_string(id) + "\nPosicion en cola: " + to_string(trabajosEnCola.size()) + "/" +
                   to_string(MAX_COLA);
        }

        return "Limite de trabajos alcanzado.\n"
               "Activos: " +
               to_string(MAX_JOBS) + " | Cola: " + to_string(MAX_COLA);
    }

    // ==========================================
    // SALIR
    // ==========================================
    if (comando == "salir" || comando == "exit") {
        return "Cerrando conexion con JUAN...";
    }

    // ==========================================
    // COMANDO INVALIDO
    // ==========================================
    return "Comando invalido. Escribe 'ayuda' para ver los comandos.";
}
void mostrarMensajeRed(const string& mensaje) {

    if (procesandoComandoTerminal) {

        // Eliminar el prompt vacío que Readline acaba de dibujar.
        cout << "\r\033[2K";

        // Mostrar la respuesta del comando.
        cout << mensaje << '\n' << flush;

        // Volver a dibujar el prompt.
        rl_on_new_line();
        rl_redisplay();

    } else {

        // Mensaje asíncrono: conservar lo que el usuario está escribiendo.
        rl_crlf();

        cout << mensaje << '\n' << flush;

        rl_on_new_line();
        rl_redisplay();
    }
}

void procesarEventoCliente(int fd, FDEventFlags eventos, sqlite3* dbConn, auto logger,
                           EventLoop& loop) {
    auto clienteIt = clientes.find(fd);

    if (clienteIt == clientes.end()) {
        return;
    }

    bool desconectar = false;

    // Detectar errores o cierre de conexión.
    if (eventos & (EPOLLERR | EPOLLHUP)) {
        desconectar = true;
    }

    // RECEPCIÓN DE DATOS
    if (!desconectar && (eventos & (EPOLLIN | EPOLLRDHUP))) {
        char bufferRed[1024];

        while (true) {
            ssize_t bytes = recv(fd, bufferRed, sizeof(bufferRed), 0);

            if (bytes > 0) {
                clienteIt->second.entrada.append(bufferRed, bytes);

                if (clienteIt->second.entrada.size() > 8192) {
                    desconectar = true;
                    break;
                }

                continue;
            }

            if (bytes == 0) {
                clienteIt->second.cerrarDespuesDeEnviar = true;
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            desconectar = true;
            break;
        }
    }

    // PROCESAR COMANDOS COMPLETOS
    if (!desconectar) {
        auto& entrada = clienteIt->second.entrada;
        size_t posicion;

        while ((posicion = entrada.find('\n')) != string::npos) {
            string comando = entrada.substr(0, posicion);
            entrada.erase(0, posicion + 1);

            if (!comando.empty() && comando.back() == '\r') {
                comando.pop_back();
            }

            if (comando.empty()) {
                continue;
            }

            mostrarMensajeRed("[RED] " + comando);

            sockaddr_in direccion{};
            socklen_t longitud = sizeof(direccion);

            if (getpeername(fd, reinterpret_cast<sockaddr*>(&direccion), &longitud) == 0) {

                char ip[INET_ADDRSTRLEN]{};
                inet_ntop(AF_INET, &direccion.sin_addr, ip, sizeof(ip));

                logger->info("Se recibió comando '{}' desde IP {}", comando, ip);
            }

            string respuesta = procesarComandoRed(comando, dbConn, logger, loop);

            clienteIt->second.salida += respuesta;

            if (comando == "salir" || comando == "exit") {
                clienteIt->second.cerrarDespuesDeEnviar = true;
            } else {
                clienteIt->second.salida += "\nJUAN-RED> ";
            }
        }
    }

    // ENVÍO DE RESPUESTAS
    if (!desconectar && (eventos & EPOLLOUT)) {
        auto& salida = clienteIt->second.salida;

        while (!salida.empty()) {
            ssize_t enviados = send(fd, salida.data(), salida.size(), MSG_NOSIGNAL);

            if (enviados > 0) {
                salida.erase(0, static_cast<size_t>(enviados));
                continue;
            }

            if (enviados == -1 && errno == EINTR) {
                continue;
            }

            if (enviados == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                break;
            }

            desconectar = true;
            break;
        }
    }

    // CERRAR CLIENTE
    if (desconectar ||
        (clienteIt->second.cerrarDespuesDeEnviar && clienteIt->second.salida.empty())) {

        auto handleIt = handlesEventos.find(fd);

        if (handleIt != handlesEventos.end()) {
            auto resultado = loop.remove_fd_event(handleIt->second);

            if (!resultado) {
                logger->error("Error retirando cliente {} del EventLoop", fd);
            }

            handlesEventos.erase(handleIt);
        }

        close(fd);
        clientes.erase(clienteIt);

        logger->info("Cliente desconectado. FD: {}", fd);
        return;
    }

    // ACTUALIZAR INTERÉS DE EVENTOS
    FDEventFlags nuevosEventos = EPOLLIN | EPOLLRDHUP;

    if (!clienteIt->second.salida.empty()) {
        nuevosEventos |= EPOLLOUT;
    }

    auto handleIt = handlesEventos.find(fd);

    if (handleIt != handlesEventos.end()) {
        auto resultado = loop.modify_fd_event(handleIt->second, nuevosEventos);

        if (!resultado) {
            logger->error("Error actualizando eventos del cliente {}", fd);

            loop.remove_fd_event(handleIt->second);
            handlesEventos.erase(handleIt);

            close(fd);
            clientes.erase(clienteIt);
        }
    }
}

int main() {

    bool ejecutando = true;
    using namespace juan;
    namespace fs = std::filesystem;

    auto logger_res = OFSOutput::open("juan_log");
    if (!logger_res) {
        std::cout << "Failed to open log file" << std::endl;
    }

    auto logger = std::make_shared<Logger>();
    if (logger_res)
        *logger = Logger(std::move(*logger_res), LogLevel::INFO);
    try {
        fs::create_directories("logs/job_output");
        fs::create_directories("logs/job_error");

        logger->info("Directorios de logs creados correctamente.");
    } catch (const fs::filesystem_error& e) {
        logger->error("Error al crear directorios: ", e.what());
        return 1;
    }

    auto db = DB::connect("data.db", logger);
    if (!db) {
        logger->error("Failed to connect to db {}", status_str(db.error()));
        return -1;
    }
    auto dbCOnn = db->db;

    auto session_res = JuanSession::init(logger, std::move(*db));
    if (!session_res) {
        logger->error("Failed to create session {}", status_str(session_res.error()));
        return -1;
    }
    auto& juan_session = *session_res;
    logger->info("Started juan session with id: {}", juan_session.get_session().id);

    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(dbCOnn,
                       "CREATE TABLE IF NOT EXISTS jobs ("
                       "id INTEGER PRIMARY KEY, "
                       "command TEXT NOT NULL, "
                       "status INTEGER NOT NULL, "
                       "queued_at INTEGER, "
                       "launched_at INTEGER, "
                       "finished_at INTEGER, "
                       "result INTEGER"
                       ") STRICT",
                       -1, &stmt, nullptr);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_stmt* insert_job_stmt;

    sqlite3_prepare_v2(dbCOnn,
                       "INSERT INTO jobs "
                       "(id,command,status) "
                       "VALUES (NULL,:cmd,:st) "
                       "RETURNING id",
                       -1, &insert_job_stmt, nullptr);

    auto cmd_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":cmd");

    auto st_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":st");

    auto get_id = [&](const string& cmd, s8 status) {
        int bind_res;

        bind_res = sqlite3_bind_text64(insert_job_stmt, cmd_idx, cmd.c_str(), cmd.length(),
                                       SQLITE_TRANSIENT, SQLITE_UTF8);

        if (bind_res != SQLITE_OK) {

            logger->error("error binding cmd {}", sqlite3_errmsg(dbCOnn));
        }

        bind_res = sqlite3_bind_int64(insert_job_stmt, st_idx, status);

        if (bind_res != SQLITE_OK) {

            logger->error("error binding st {}", sqlite3_errmsg(dbCOnn));
        }

        auto ret = sqlite3_step(insert_job_stmt);

        int64_t id = -1L;

        if (ret == SQLITE_ROW) {

            id = sqlite3_column_int64(insert_job_stmt, 0);

        } else {

            logger->error("error inserting {}", sqlite3_errmsg(dbCOnn));
        }

        ret = sqlite3_step(insert_job_stmt);

        if (ret == SQLITE_DONE) {

            sqlite3_reset(insert_job_stmt);

            return id;
        }

        return -1L;
    };

    auto loop_res = EventLoop::create(logger);

    if (!loop_res) {
        logger->error("Error al crear EventLoop");
        return 1;
    }

    auto& loop = *loop_res;

    int servidor = crearServidor(5050, logger);

    if (servidor == -1) {
        return 1;
    }

    auto servidorHandle = loop.add_fd_event(servidor, EPOLLIN, [&, logger](FDEventFlags eventos) {
        if (!(eventos & EPOLLIN)) {
            return;
        }

        while (true) {

            sockaddr_in direccionCliente{};
            socklen_t longitud = sizeof(direccionCliente);

            int cliente = accept4(servidor, reinterpret_cast<sockaddr*>(&direccionCliente),
                                  &longitud, SOCK_NONBLOCK | SOCK_CLOEXEC);

            if (cliente == -1) {

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }

                if (errno == EINTR) {
                    continue;
                }

                logger->error("Error aceptando cliente: {}", strerror(errno));

                break;
            }

            clientes.emplace(cliente, ClienteTCP{});

            auto eventoCliente =
                loop.add_fd_event(cliente, EPOLLIN | EPOLLRDHUP | EPOLLOUT,
                                  [cliente, dbCOnn, logger, &loop](FDEventFlags eventos) {
                                      procesarEventoCliente(cliente, eventos, dbCOnn, logger, loop);
                                  });

            if (!eventoCliente) {
                logger->error("Error registrando cliente {} en EventLoop", cliente);

                close(cliente);
                clientes.erase(cliente);
                continue;
            }

            handlesEventos[cliente] = *eventoCliente;
            char ip[INET_ADDRSTRLEN]{};

            inet_ntop(AF_INET, &direccionCliente.sin_addr, ip, sizeof(ip));

            mostrarMensajeRed("[RED] Cliente conectado: " + string(ip));

            clientes[cliente].salida = "Conectado al servidor JUAN.\nJUAN-RED> ";

            logger->info("cliente conectado con ip {}", ip);
        }
    });

    if (!servidorHandle) {

        logger->error("Error registrando servidor en EventLoop");

        close(servidor);
        return 1;
    }

    cout << "Servidor TCP iniciado en puerto 5050" << endl;

    cout << "JUAN iniciado. PID: " << getpid() << endl;

    // Inicializar Readline en modo callback.
    rl_callback_handler_install("JUAN> ", recibirLinea);
    // Registrar STDIN en el EventLoop.
    string linea;
    auto stdinHandle = loop.add_fd_event(STDIN_FILENO, EPOLLIN, [&, logger](FDEventFlags eventos) {
        if (eventos & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
            rl_callback_read_char();
        }

        // Procesar los comandos recibidos desde la terminal
        procesandoComandoTerminal = true;

        while (!comandosTerminal.empty()) {
            linea = comandosTerminal.front();
            comandosTerminal.pop();

            logger->info("Got commandline: \"{}\"", linea);

            if (linea == "salir" || linea == "exit") {
                mostrarMensajeRed("Cerrando JUAN...");
                ejecutando = false;
                break;
            }

            else if (linea == "ayuda" || linea == "help") {

                string ayuda = "Comandos disponibles:\n"
                               "  ayuda          - Mostrar ayuda\n"
                               "  status         - Mostrar estados\n"
                               "  correr         - Ejecutar comando Linux\n"
                               "  cancelar <id>  - Cancelar un proceso en ejecucion\n"
                               "  corriendo      - Mostrar todos los procesos activos\n"
                               "  cola           - Mostrar todos los procesos en la cola\n"
                               "  jobs           - Mostrar todos los procesos\n"
                               "  salir          - Cerrar JUAN";

                mostrarMensajeRed(ayuda);
            }

            else if (linea == "status") {

                string respuesta;
                respuesta += "JUAN funcionando...\n";
                respuesta += "Clientes conectados: " + to_string(clientes.size()) + "\n";
                respuesta += "Jobs activos: " + to_string(RuningJobs.size()) + "/" +
                             to_string(MAX_JOBS) + "\n";
                respuesta +=
                    "Jobs en cola: " + to_string(trabajosEnCola.size()) + "/" + to_string(MAX_COLA);

                mostrarMensajeRed(respuesta);
            } else if (linea.rfind("cancelar ", 0) == 0) {

                string idTexto = linea.substr(9);

                if (idTexto.empty()) {

                    mostrarMensajeRed("Debes indicar el ID del job.");

                } else {

                    try {

                        JobID id = stoll(idTexto);

                        if (cancelarEnCola(id, dbCOnn, logger)) {

                            mostrarMensajeRed("Job " + to_string(id) +
                                              " cancelado y eliminado de la cola.");

                        } else {

                            auto it = mapaIdPid.find(id);

                            if (it == mapaIdPid.end()) {

                                mostrarMensajeRed("No existe un job con ID " + to_string(id) + ".");

                            } else {

                                // Buscar el Job
                                bool yaCancelando = false;

                                for (auto& job : RuningJobs) {

                                    if (job.id == id) {

                                        if (job.status == CANCELING) {
                                            yaCancelando = true;
                                        } else {
                                            job.status = CANCELING;
                                        }

                                        break;
                                    }
                                }

                                if (yaCancelando) {

                                    mostrarMensajeRed("El job " + to_string(id) +
                                                      " ya tiene una cancelacion pendiente.");

                                } else if (enviarSenalPidfd(mapaIdPidfd.at(id), SIGTERM) == 0) {

                                    if (programarCancelacion(id, logger, loop)) {

                                        mostrarMensajeRed("Cancelacion solicitada para job " +
                                                          to_string(id) + ". Timeout: 5000 ms.");

                                    } else {

                                        mostrarMensajeRed(
                                            "SIGTERM enviado, pero no se pudo programar "
                                            "el timeout de cancelacion.");
                                    }

                                } else {

                                    logger->error("Error al enviar SIGTERM: {}", strerror(errno));

                                    mostrarMensajeRed("Error al solicitar la cancelacion del job " +
                                                      to_string(id) + ".");
                                }
                            }
                        }

                    } catch (const exception& e) {

                        mostrarMensajeRed("ID invalido.");
                    }
                }
            } else if (linea == "corriendo") {

                if (RuningJobs.empty()) {

                    mostrarMensajeRed("No hay jobs ejecutandose.");

                } else {

                    string mensaje = "Jobs activos:\n";

                    for (const auto& job : RuningJobs) {

                        auto it = mapaIdPid.find(job.id);

                        pid_t pid = (it != mapaIdPid.end()) ? it->second : -1;

                        mensaje += "ID: " + to_string(job.id) + " | PID: " + to_string(pid) +
                                   " | Comando: " + job.command +
                                   " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";
                    }

                    mostrarMensajeRed(mensaje);
                }
            } else if (linea == "cola") {

                if (trabajosEnCola.empty()) {

                    mostrarMensajeRed("No hay Jobs en cola.");

                } else {

                    string mensaje = "Jobs en cola:\n";

                    auto auxiliar = trabajosEnCola;

                    while (!auxiliar.empty()) {

                        const auto& job = auxiliar.front();

                        mensaje += "ID: " + to_string(job.id) + " | PID: --" +
                                   " | Comando: " + job.command +
                                   " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";

                        auxiliar.pop();
                    }

                    mostrarMensajeRed(mensaje);
                }
            }

            else if (linea == "jobs") {

                string mensaje;

                // ==========================================
                // JOBS ACTIVOS
                // ==========================================

                if (RuningJobs.empty()) {

                    mensaje += "No hay jobs ejecutandose.\n";

                } else {

                    mensaje += "Jobs activos:\n";

                    for (const auto& job : RuningJobs) {

                        auto it = mapaIdPid.find(job.id);

                        pid_t pid = (it != mapaIdPid.end()) ? it->second : -1;

                        mensaje += "ID: " + to_string(job.id) + " | PID: " + to_string(pid) +
                                   " | Comando: " + job.command +
                                   " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";
                    }
                }

                // ==========================================
                // JOBS EN COLA
                // ==========================================

                if (trabajosEnCola.empty()) {

                    mensaje += "\nNo hay Jobs en cola.\n";

                } else {

                    mensaje += "\nJobs en cola:\n";

                    auto auxiliar = trabajosEnCola;

                    while (!auxiliar.empty()) {

                        const auto& job = auxiliar.front();

                        mensaje += "ID: " + to_string(job.id) + " | PID: --" +
                                   " | Comando: " + job.command +
                                   " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";

                        auxiliar.pop();
                    }
                }

                mostrarMensajeRed(mensaje);
            }

            else if (linea.rfind("correr ", 0) == 0) {

                string comando = linea.substr(7);

                if (comando.empty()) {

                    mostrarMensajeRed("Debes escribir un comando.");

                }

                // Primero comprobar si hay espacio para ejecutar inmediatamente.
                else if (RuningJobs.size() < MAX_JOBS) {

                    JobID id = get_id(comando, LAUNCHED);

                    if (id == -1) {

                        mostrarMensajeRed("No se pudo registrar el job en SQLite.");

                    } else {

                        Job nuevoJob;
                        nuevoJob.id = id;
                        nuevoJob.command = comando;
                        nuevoJob.status = LAUNCHED;

                        correr(nuevoJob, logger, loop);
                    }
                }

                // Si no hay espacio activo, comprobar si puede entrar en cola.
                else if (trabajosEnCola.size() < MAX_COLA) {

                    JobID id = get_id(comando, QUEUED);

                    if (id == -1) {

                        mostrarMensajeRed("No se pudo registrar el job en SQLite.");

                    } else {

                        Job nuevoJob;
                        nuevoJob.id = id;
                        nuevoJob.command = comando;
                        nuevoJob.status = QUEUED;

                        trabajosEnCola.push(nuevoJob);

                        string mensaje = "Job creado en cola.\n"
                                         "Job ID: " +
                                         to_string(id) + "\n" +
                                         "Posicion en cola: " + to_string(trabajosEnCola.size()) +
                                         "/" + to_string(MAX_COLA);

                        mostrarMensajeRed(mensaje);
                    }
                }

                else {

                    string mensaje = "Limite de trabajos alcanzado.\n"
                                     "Activos: " +
                                     to_string(MAX_JOBS) + " | Cola: " + to_string(MAX_COLA);

                    mostrarMensajeRed(mensaje);
                }
            }

            else if (linea == "correr") {

                mostrarMensajeRed("Debes escribir un comando despues de 'correr'.");
            }

            else if (linea.empty()) {
            }

            else {

                mostrarMensajeRed("Comando invalido.");
            }
        }

        procesandoComandoTerminal = false;

        if (finEntradaTerminal) {
            ejecutando = false;
        }
    });
    if (!stdinHandle) {
        logger->error("Error registrando STDIN en EventLoop");
        close(servidor);
        return 1;
    }

    while (ejecutando) {

        auto resultado = loop.run_once();

        if (!resultado) {
            logger->error("Error ejecutando EventLoop");
            break;
        }
    }

    rl_callback_handler_remove();

    // Retirar STDIN del EventLoop.
    if (!loop.remove_fd_event(*stdinHandle)) {
        logger->error("Error retirando STDIN");
    }

    // Retirar servidor del EventLoop.
    if (!loop.remove_fd_event(*servidorHandle)) {
        logger->error("Error retirando servidor");
    }

    // Cerrar clientes TCP.
    for (auto& [fd, cliente] : clientes) {

        auto it = handlesEventos.find(fd);

        if (it != handlesEventos.end()) {
            (void)loop.remove_fd_event(it->second);
            handlesEventos.erase(it);
        }

        close(fd);
    }

    clientes.clear();

    // Cerrar servidor.
    close(servidor);

    // Liberar sentencia SQLite.
    sqlite3_finalize(insert_job_stmt);

    return 0;
}
