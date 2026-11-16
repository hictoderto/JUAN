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
#include <sys/wait.h>
#include <types.h>
#include <unistd.h>
#include <unordered_map>

using namespace juan;
using namespace std;
const int MAX_JOBS = 5;
const int MAX_COLA = 5;
using Reloj = std::chrono::steady_clock;
void mostrarMensajeRed(const string& mensaje);

map<JobID, Reloj::time_point> plazosCancelacion;

unordered_map<int, ClienteTCP> clientes;

const int TIMEOUT_CANCELACION_MS = 5000;

queue<Job> trabajosEnCola;
vector<Job> RuningJobs;
map<JobID, pid_t> mapaIdPid;
int sigchld_pipe[2];

queue<string> comandosTerminal;
bool finEntradaTerminal = false;
bool procesandoComandoTerminal = false;

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

void manejarSIGCHLD(int) {
    int errorAnterior = errno;
    char byte = 1;

    // Avisar al ciclo principal que cambió el estado de un hijo.
    (void)write(sigchld_pipe[1], &byte, 1);

    errno = errorAnterior;
}
int calcularTimeout() {
    if (plazosCancelacion.empty()) {
        return -1; // Esperar hasta el próximo evento.
    }

    auto ahora = Reloj::now();
    auto proximo = Reloj::time_point::max();

    for (const auto& [id, plazo] : plazosCancelacion) {
        if (plazo < proximo) {
            proximo = plazo;
        }
    }

    auto restante = std::chrono::duration_cast<std::chrono::milliseconds>(proximo - ahora).count();

    if (restante <= 0) {
        return 0;
    }

    return static_cast<int>(std::min<long long>(restante, INT_MAX));
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

void revisarCancelaciones(auto logger) {
    auto ahora = Reloj::now();

    for (auto it = plazosCancelacion.begin(); it != plazosCancelacion.end();) {

        if (ahora < it->second) {
            ++it;
            continue;
        }

        JobID id = it->first;
        auto pidIt = mapaIdPid.find(id);

        if (pidIt != mapaIdPid.end()) {
            pid_t pid = pidIt->second;

            if (kill(pid, SIGKILL) == 0) {
                mostrarMensajeRed("[TIMEOUT] Job " + to_string(id) + ": se envio SIGKILL.");
            } else if (errno != ESRCH) {
                logger->error("error en kill SIGKILL");
            }
        }

        it = plazosCancelacion.erase(it);
    }
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

void correr(Job nuevoJob, auto logger) {
    pid_t child = fork();

    if (child == -1) {
        logger->error("Error al crear el proceso: {}", strerror(errno));
        return;
    }

    else if (child == 0) {
        string logFileNameOut = "logs/job_output/" + to_string(nuevoJob.id) + ".log";
        string logFileNameErr = "logs/job_error/" + to_string(nuevoJob.id) + ".log";

        // Inicializamos el archivo de salida para stdout y stderr
        int salida_out = open(logFileNameOut.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        int salida_err = open(logFileNameErr.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (salida_out == -1 || salida_err == -1) {

            logger->info("Archivo de logs abierto para job {}", nuevoJob.id);

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

    else {

        RuningJobs.push_back(nuevoJob);

        mapaIdPid[nuevoJob.id] = child;

        logger->info("Job creado. ID: {} | PID: {} | Jobs activos: {}/{}", nuevoJob.id, child,
                     RuningJobs.size(), MAX_JOBS);
    }
}

void actualizarJobs(auto logger) {
    vector<Job> trabajosPorIniciar;

    for (auto it = RuningJobs.begin(); it != RuningJobs.end();) {
        auto mapaIt = mapaIdPid.find(it->id);

        if (mapaIt == mapaIdPid.end()) {
            ++it;
            continue;
        }

        pid_t pid = mapaIt->second;
        int estado = 0;

        pid_t resultado = waitpid(pid, &estado, WNOHANG);

        if (resultado == 0) {
            // El proceso todavía está ejecutándose.
            ++it;
            continue;
        }

        if (resultado == -1) {
            if (errno == EINTR) {
                ++it;
                continue;
            }

            // ECHILD significa que ya no podemos esperar por este hijo.
            // Se registra el error y se retira de las estructuras activas.
            logger->error("Error en waitpid para job {}: {}", it->id, strerror(errno));

            it->status = FAILED;
            it->result = -1;
        } else {
            mostrarMensajeRed("[JOB TERMINADO] ID: " + to_string(it->id) +
                              " | PID: " + to_string(pid) + " | Comando: " + it->command);

            if (WIFEXITED(estado)) {
                int codigo = WEXITSTATUS(estado);

                mostrarMensajeRed("Codigo: " + to_string(codigo));

                it->result = codigo;
                it->status = (codigo == 0) ? SUCCEDED : FAILED;

            } else if (WIFSIGNALED(estado)) {
                int senal = WTERMSIG(estado);

                mostrarMensajeRed("Terminado por senal: " + to_string(senal));

                it->result = -senal;
                it->status = (it->status == CANCELING) ? CANCELED : FAILED;
            } else {
                it->status = FAILED;
                it->result = -1;
            }
        }

        Job terminado = *it;

        mapaIdPid.erase(mapaIt);
        plazosCancelacion.erase(terminado.id);

        it = RuningJobs.erase(it);

        // Guardamos el siguiente trabajo, pero todavía no lo lanzamos.
        if (!trabajosEnCola.empty()) {
            trabajosPorIniciar.push_back(trabajosEnCola.front());
            trabajosEnCola.pop();
        }
    }

    // Ya terminó el recorrido del vector. Ahora es seguro hacer push_back.
    for (const auto& siguiente : trabajosPorIniciar) {
        correr(siguiente, logger);
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

string procesarComandoRed(const string& comando, sqlite3* dbConn, auto logger) {

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
        actualizarJobs(logger);

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

            pid_t pid = it->second;

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

            if (kill(pid, SIGTERM) == 0) {

                plazosCancelacion[id] =
                    Reloj::now() + std::chrono::milliseconds(TIMEOUT_CANCELACION_MS);

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

        actualizarJobs(logger);

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

        actualizarJobs(logger);

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

        actualizarJobs(logger);

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

            correr(nuevoJob, logger);

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
    if (pipe2(sigchld_pipe, O_NONBLOCK | O_CLOEXEC) == -1) {
        logger->error("pipe2");
        return 1;
    }

    struct sigaction sa {};
    sa.sa_handler = manejarSIGCHLD;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        logger->error("sigaction");
        close(sigchld_pipe[0]);
        close(sigchld_pipe[1]);
        return 1;
    }

    // Crear instancia de epoll
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);

    if (epoll_fd == -1) {
        logger->error("Error al crear epoll: {}", strerror(errno));
        close(sigchld_pipe[0]);
        close(sigchld_pipe[1]);
        return 1;
    }

    int servidor = crearServidor(5050, logger);

    if (servidor == -1) {
        close(epoll_fd);
        return 1;
    }

    struct epoll_event eventoServidor {};

    eventoServidor.events = EPOLLIN;
    eventoServidor.data.fd = servidor;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, servidor, &eventoServidor) == -1) {

        logger->error("Error registrando servidor: {}", strerror(errno));

        close(servidor);
        close(epoll_fd);

        return 1;
    }

    cout << "Servidor TCP iniciado en puerto 5050" << endl;

    // Registrar stdin y el self-pipe
    struct epoll_event evento {};

    evento.events = EPOLLIN;
    evento.data.fd = STDIN_FILENO;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &evento) == -1) {
        logger->error("Error registrando stdin: {}", strerror(errno));
        close(epoll_fd);
        close(sigchld_pipe[0]);
        close(sigchld_pipe[1]);
        return 1;
    }

    evento.events = EPOLLIN;
    evento.data.fd = sigchld_pipe[0];

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sigchld_pipe[0], &evento) == -1) {
        logger->error("Error registrando self-pipe: {}", strerror(errno));
        close(epoll_fd);
        close(sigchld_pipe[0]);
        close(sigchld_pipe[1]);
        return 1;
    }

    cout << "JUAN iniciado. PID: " << getpid() << endl;

    // Inicializar Readline en modo callback.
    rl_callback_handler_install("JUAN> ", recibirLinea);

    string linea;

    constexpr int MAX_EVENTOS_EPOLL = 64;
    struct epoll_event eventos[MAX_EVENTOS_EPOLL];

    while (ejecutando) {

        // Esperar eventos usando el timeout de las cancelaciones.
        int resultado = epoll_wait(epoll_fd, eventos, MAX_EVENTOS_EPOLL, calcularTimeout());

        if (resultado == -1) {
            if (errno == EINTR) {
                revisarCancelaciones(logger);
                continue;
            }

            logger->error("Error en epoll_wait: {}", strerror(errno));
            break;
        }

        // Procesar todos los eventos recibidos.
        for (int i = 0; i < resultado; i++) {

            int fd = eventos[i].data.fd;
            uint32_t flags = eventos[i].events;

            // NUEVAS CONEXIONES TCP
            if (fd == servidor && (flags & EPOLLIN)) {

                while (true) {

                    sockaddr_in direccionCliente{};
                    socklen_t longitud = sizeof(direccionCliente);

                    int cliente = accept4(servidor, reinterpret_cast<sockaddr*>(&direccionCliente),
                                          &longitud, SOCK_NONBLOCK | SOCK_CLOEXEC);

                    if (cliente == -1) {

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        if (errno == EINTR)
                            continue;

                        logger->error("Error aceptando cliente: {}", strerror(errno));

                        break;
                    }

                    // Registrar nuevo cliente en epoll
                    epoll_event eventoCliente{};

                    eventoCliente.events = EPOLLIN | EPOLLRDHUP | EPOLLOUT;
                    eventoCliente.data.fd = cliente;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cliente, &eventoCliente) == -1) {

                        close(cliente);
                        continue;
                    }

                    clientes.emplace(cliente, ClienteTCP{});

                    char ip[INET_ADDRSTRLEN];

                    inet_ntop(AF_INET, &direccionCliente.sin_addr, ip, sizeof(ip));

                    mostrarMensajeRed("[RED] Cliente conectado: " + string(ip));

                    clientes[cliente].salida = "Conectado al servidor JUAN.\nJUAN-RED> ";
                    logger->info("cliente conectado con ip {}", ip);
                }
            }

            // RECEPCION DE DATOS TCP
            auto clienteIt = clientes.find(fd);

            if (clienteIt != clientes.end()) {

                bool desconectar = false;

                if (flags & (EPOLLERR | EPOLLHUP)) {
                    desconectar = true;
                }

                // Leer datos disponibles
                if (!desconectar && (flags & (EPOLLIN | EPOLLRDHUP))) {

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

                        if (errno == EINTR)
                            continue;

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;

                        desconectar = true;
                        break;
                    }
                }

                // Procesar comandos completos
                if (!desconectar) {

                    auto& entrada = clienteIt->second.entrada;

                    size_t posicion;

                    while ((posicion = entrada.find('\n')) != string::npos) {

                        string comando = entrada.substr(0, posicion);

                        entrada.erase(0, posicion + 1);

                        if (!comando.empty() && comando.back() == '\r')
                            comando.pop_back();

                        if (comando.empty())
                            continue;

                        mostrarMensajeRed("[RED] " + comando);
                        sockaddr_in direccion{};
                        socklen_t longitud = sizeof(direccion);

                        if (getpeername(fd, reinterpret_cast<sockaddr*>(&direccion), &longitud) ==
                            0) {
                            char ip[INET_ADDRSTRLEN]{};
                            inet_ntop(AF_INET, &direccion.sin_addr, ip, sizeof(ip));

                            logger->info("Se recibió comando '{}' desde IP {}", comando, ip);
                        }

                        string respuesta = procesarComandoRed(comando, dbCOnn, logger);

                        clienteIt->second.salida += respuesta;

                        if (comando == "salir" || comando == "exit") {

                            clienteIt->second.cerrarDespuesDeEnviar = true;

                        } else {

                            clienteIt->second.salida += "\nJUAN-RED> ";
                        }
                    }
                }

                // ENVIO DE RESPUESTAS TCP
                if (!desconectar && (flags & EPOLLOUT)) {

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

                // CERRAR CLIENTE SI ES NECESARIO
                if (desconectar ||
                    (clienteIt->second.cerrarDespuesDeEnviar && clienteIt->second.salida.empty())) {

                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

                    close(fd);
                    clientes.erase(clienteIt);

                    continue;
                }

                // ACTUALIZAR EVENTOS DE EPOLL
                epoll_event eventoCliente{};

                eventoCliente.events = EPOLLIN | EPOLLRDHUP;

                // Activar escritura solamente cuando hay datos pendientes.
                if (!clienteIt->second.salida.empty()) {
                    eventoCliente.events |= EPOLLOUT;
                }

                eventoCliente.data.fd = fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &eventoCliente) == -1) {

                    logger->error("Error actualizando eventos del cliente: {}", strerror(errno));

                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

                    close(fd);
                    clientes.erase(clienteIt);

                    continue;
                }

                if (desconectar ||
                    (clienteIt->second.cerrarDespuesDeEnviar && clienteIt->second.salida.empty())) {

                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

                    close(fd);
                    clientes.erase(clienteIt);

                    continue;
                }
            }

            // Evento 1: terminó o cambió el estado de un job.
            if (fd == sigchld_pipe[0] && (flags & (EPOLLIN | EPOLLERR | EPOLLHUP))) {

                char notificaciones[128];

                // Vaciar todas las notificaciones pendientes.
                while (read(sigchld_pipe[0], notificaciones, sizeof(notificaciones)) > 0) {
                }

                // Actualizar los trabajos que terminaron.
                actualizarJobs(logger);
            }

            // Evento 2: hay entrada en la terminal.
            if (fd == STDIN_FILENO && (flags & (EPOLLIN | EPOLLHUP | EPOLLERR))) {

                // Readline procesa los caracteres disponibles.
                rl_callback_read_char();

                // Procesar las líneas completas recibidas.
                // Procesar las líneas completas recibidas.
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
                        actualizarJobs(logger);

                        string respuesta;
                        respuesta += "JUAN funcionando...\n";
                        respuesta += "Clientes conectados: " + to_string(clientes.size()) + "\n";
                        respuesta += "Jobs activos: " + to_string(RuningJobs.size()) + "/" +
                                     to_string(MAX_JOBS) + "\n";
                        respuesta += "Jobs en cola: " + to_string(trabajosEnCola.size()) + "/" +
                                     to_string(MAX_COLA);

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

                                        mostrarMensajeRed("No existe un job con ID " +
                                                          to_string(id) + ".");

                                    } else {

                                        pid_t pid = it->second;

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

                                            mostrarMensajeRed(
                                                "El job " + to_string(id) +
                                                " ya tiene una cancelacion pendiente.");

                                        } else if (kill(pid, SIGTERM) == 0) {

                                            plazosCancelacion[id] =
                                                Reloj::now() +
                                                std::chrono::milliseconds(TIMEOUT_CANCELACION_MS);

                                            mostrarMensajeRed("Cancelacion solicitada para job " +
                                                              to_string(id) +
                                                              ". Timeout: 5000 ms.");

                                        } else {

                                            logger->error("Error al enviar SIGTERM: {}",
                                                          strerror(errno));

                                            mostrarMensajeRed(
                                                "Error al solicitar la cancelacion del job " +
                                                to_string(id) + ".");
                                        }
                                    }
                                }

                            } catch (const exception& e) {

                                mostrarMensajeRed("ID invalido.");
                            }
                        }
                    } else if (linea == "corriendo") {

                        actualizarJobs(logger);

                        if (RuningJobs.empty()) {

                            mostrarMensajeRed("No hay jobs ejecutandose.");

                        } else {

                            string mensaje = "Jobs activos:\n";

                            for (const auto& job : RuningJobs) {

                                auto it = mapaIdPid.find(job.id);

                                pid_t pid = (it != mapaIdPid.end()) ? it->second : -1;

                                mensaje +=
                                    "ID: " + to_string(job.id) + " | PID: " + to_string(pid) +
                                    " | Comando: " + job.command +
                                    " | Estado: " + string(Jobs_tatus_str(job.status)) + "\n";
                            }

                            mostrarMensajeRed(mensaje);
                        }
                    } else if (linea == "cola") {

                        actualizarJobs(logger);

                        if (trabajosEnCola.empty()) {

                            mostrarMensajeRed("No hay Jobs en cola.");

                        } else {

                            string mensaje = "Jobs en cola:\n";

                            auto auxiliar = trabajosEnCola;

                            while (!auxiliar.empty()) {

                                const auto& job = auxiliar.front();

                                mensaje += "ID: " + to_string(job.id) + " | PID: --" +
                                           " | Comando: " + job.command +
                                           " | Estado: " + string(Jobs_tatus_str(job.status)) +
                                           "\n";

                                auxiliar.pop();
                            }

                            mostrarMensajeRed(mensaje);
                        }
                    }

                    else if (linea == "jobs") {

                        actualizarJobs(logger);

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

                                mensaje +=
                                    "ID: " + to_string(job.id) + " | PID: " + to_string(pid) +
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
                                           " | Estado: " + string(Jobs_tatus_str(job.status)) +
                                           "\n";

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

                                correr(nuevoJob, logger);
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
                                                 to_string(id) + "\n" + "Posicion en cola: " +
                                                 to_string(trabajosEnCola.size()) + "/" +
                                                 to_string(MAX_COLA);

                                mostrarMensajeRed(mensaje);
                            }
                        }

                        else {

                            string mensaje = "Limite de trabajos alcanzado.\n"
                                             "Activos: " +
                                             to_string(MAX_JOBS) +
                                             " | Cola: " + to_string(MAX_COLA);

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
                    cout << "\nEntrada estándar cerrada. Cerrando JUAN..." << endl;
                    ejecutando = false;
                }
            }
        }
        if (!ejecutando) {
            break;
        }
        revisarCancelaciones(logger);
    }

    rl_callback_handler_remove();
    close(epoll_fd);
    close(sigchld_pipe[0]);
    close(sigchld_pipe[1]);

    return 0;
}
