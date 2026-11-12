#include "database.h"
#include "job.h"
#include "result.h"

#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <poll.h>
#include <queue>
#include <signal.h>
#include <sqlite3.h>
#include <string>
#include <sys/wait.h>
#include <types.h>
#include <unistd.h>
#include <vector>

#include <juan_session.h>
#include <logger.h>
#include <timestamp.h>

using namespace juan;
using namespace std;
const int MAX_JOBS = 5;
const int MAX_COLA = 5;

queue<Job> trabajosEnCola;
vector<Job> RuningJobs;
map<JobID, pid_t> mapaIdPid;
int sigchld_pipe[2];

void manejarSIGCHLD(int) {
    int errorAnterior = errno;
    char byte = 1;

    // Avisar al ciclo principal que cambió el estado de un hijo.
    (void)write(sigchld_pipe[1], &byte, 1);

    errno = errorAnterior;
}

// Cambia el estado de un Job a su representación en string.
const char* Jobs_status_str(JobStatus status) {
    switch (status) {
        case QUEUED:
            return "En cola";
        case LAUNCHED:
            return "Lanzado";
        case RUNNING:
            return "Ejecutandose";
        case SUCCEDED:
            return "Exitoso";
        case REJECTED:
            return "Rechazado";
        case CANCELING:
            return "Cancelando";
        case CANCELED:
            return "Cancelado";
        case FAILED:
            return "Fallido";
        default:
            return "Desconocido";
    }
}

// Guarda el lanzamiento de un Job en la base de datos.
bool guardarLanzamiento(sqlite3* dbConn, JobID id,
                        const shared_ptr<Logger>& logger) {
    const char* sql =
        "UPDATE jobs SET status = ?, launched_at = ? WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(dbConn, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        logger->error("Error preparando lanzamiento: {}", sqlite3_errmsg(dbConn));
        return false;
    }

    rc = sqlite3_bind_int(stmt, 1, LAUNCHED);
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 2, Timestamp::now().as_s64());
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 3, id);

    if (rc == SQLITE_OK)
        rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        logger->error("Error guardando lanzamiento: {}", sqlite3_errmsg(dbConn));
        return false;
    }

    return true;
}

// Guarda la finalización de un Job en la base de datos.
bool guardarFinalizacion(sqlite3* dbConn, JobID id, JobStatus status,
                         bool tieneCodigoSalida, int codigoSalida,
                         const shared_ptr<Logger>& logger) {
    const char* sql =
        "UPDATE jobs SET status = ?, finished_at = ?, result = ? WHERE id = ?";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(dbConn, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        logger->error("Error preparando finalizacion: {}", sqlite3_errmsg(dbConn));
        return false;
    }

    rc = sqlite3_bind_int(stmt, 1, status);
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 2, Timestamp::now().as_s64());
    if (rc == SQLITE_OK) {
        rc = tieneCodigoSalida
            ? sqlite3_bind_int(stmt, 3, codigoSalida)
            : sqlite3_bind_null(stmt, 3);
    }
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 4, id);

    if (rc == SQLITE_OK)
        rc = sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        logger->error("Error guardando finalizacion: {}", sqlite3_errmsg(dbConn));
        return false;
    }

    return true;
}

void registrarResultadoProceso(Job& job, int estado, sqlite3* dbConn,
                               const shared_ptr<Logger>& logger) {
    bool tieneCodigoSalida = false;
    int codigoSalida = 0;

    if (WIFEXITED(estado)) {
        codigoSalida = WEXITSTATUS(estado);
        tieneCodigoSalida = true;
        job.result = static_cast<u8>(codigoSalida);
        job.status = (codigoSalida == 0) ? SUCCEDED : FAILED;
    } else if (WIFSIGNALED(estado)) {
        job.status = (job.status == CANCELING) ? CANCELED : FAILED;
        job.result.reset();
    } else {
        job.status = FAILED;
        job.result.reset();
    }

    guardarFinalizacion(dbConn, job.id, job.status, tieneCodigoSalida,
                        codigoSalida, logger);
}

bool revisarJobsInconclusos(sqlite3* dbConn, const shared_ptr<Logger>& logger) {
    const char* sql = "SELECT id, command FROM jobs WHERE status = ?";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(dbConn, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        logger->error("Error preparando busqueda de jobs inconclusos: {}",
                      sqlite3_errmsg(dbConn));
        return false;
    }

    rc = sqlite3_bind_int(stmt, 1, LAUNCHED);
    if (rc != SQLITE_OK) {
        logger->error("Error buscando jobs inconclusos: {}", sqlite3_errmsg(dbConn));
        sqlite3_finalize(stmt);
        return false;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        JobID id = sqlite3_column_int64(stmt, 0);
        const auto* command_text = sqlite3_column_text(stmt, 1);
        string command = command_text
            ? reinterpret_cast<const char*>(command_text)
            : "(comando no disponible)";

        logger->warn(
            "El job {} ({}) quedo en estado LAUNCHED; no se registro su finalizacion "
            "y el proceso pudo interrumpirse en la ejecucion anterior.",
            id, command);
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        logger->error("Error leyendo jobs inconclusos: {}", sqlite3_errmsg(dbConn));
        return false;
    }

    return true;
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

    const char* sql =
        "UPDATE jobs SET status = ?, finished_at = ?, result = NULL WHERE id = ?";

    if (sqlite3_prepare_v2(dbConn, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        logger->error("Error preparando actualizacion: {}", sqlite3_errmsg(dbConn));
        return true; // Se eliminó de la cola, aunque falló SQLite.
    }

    int rc = sqlite3_bind_int(stmt, 1, CANCELED);
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 2, Timestamp::now().as_s64());
    if (rc == SQLITE_OK)
        rc = sqlite3_bind_int64(stmt, 3, id);

    if (rc == SQLITE_OK)
        rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        logger->error("Error actualizando job:: {}", sqlite3_errmsg(dbConn));
    }

    sqlite3_finalize(stmt);

    return true;
}

void correr(Job nuevoJob, sqlite3* dbConn, const shared_ptr<Logger>& logger) {
    pid_t child = fork();

    if (child == -1) {
        cout << "Error al crear el proceso." << endl;
        logger->error("fork fallo para el job {}: {}", nuevoJob.id, errno);
        guardarFinalizacion(dbConn, nuevoJob.id, FAILED, false, 0, logger);
    }

    else if (child == 0) {
        string logFileNameOut = "logs/job_output/" + to_string(nuevoJob.id) + ".log";
        string logFileNameErr = "logs/job_error/" + to_string(nuevoJob.id) + ".log";

        // Inicializamos el archivo de salida para stdout y stderr
        int salida_out = open(logFileNameOut.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        int salida_err = open(logFileNameErr.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

        if (salida_out == -1 || salida_err == -1) {

            perror("open");

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

        nuevoJob.status = LAUNCHED;

        guardarLanzamiento(dbConn, nuevoJob.id, logger);

        RuningJobs.push_back(nuevoJob);

        mapaIdPid[nuevoJob.id] = child;

        cout << "Job creado." << endl;

        cout << "PID: " << child << endl;

        cout << "job id: " << nuevoJob.id << endl;

        cout << "Jobs activos: " << RuningJobs.size() << "/" << MAX_JOBS << endl;
    }
}

void actualizarJobs(sqlite3* dbConn, const shared_ptr<Logger>& logger) {

    for (auto it = RuningJobs.begin(); it != RuningJobs.end();) {

        auto mapaIt = mapaIdPid.find(it->id);

        if (mapaIt == mapaIdPid.end()) {
            ++it;
            continue;
        }

        pid_t pid = mapaIt->second;

        int estado;

        pid_t resultado = waitpid(pid, &estado, WNOHANG);

        if (resultado == 0) {

            ++it;
        }

        else if (resultado == pid) {

            cout << "\n[JOB TERMINADO]" << endl;

            cout << "ID: " << it->id << endl;

            cout << "PID: " << pid << endl;

            cout << "Comando: " << it->command << endl;

            if (WIFEXITED(estado)) {
                cout << "Codigo: " << WEXITSTATUS(estado) << endl;
            } else if (WIFSIGNALED(estado)) {
                cout << "Terminado por senal: " << WTERMSIG(estado) << endl;
            }

            registrarResultadoProceso(*it, estado, dbConn, logger);

            mapaIdPid.erase(mapaIt);

            it = RuningJobs.erase(it);
            if (!trabajosEnCola.empty()) {
                Job siguiente = trabajosEnCola.front();
                trabajosEnCola.pop();

                correr(siguiente, dbConn, logger);
            }

            cout << "JUAN> " << flush;
        }

        else {

            ++it;
        }
    }
}

int main() {

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

    if (!revisarJobsInconclusos(dbCOnn, logger)) {
        return 1;
    }

    sqlite3_stmt* insert_job_stmt;

    sqlite3_prepare_v2(dbCOnn,
                       "INSERT INTO jobs "
                       "(id,command,status,queued_at) "
                       "VALUES (NULL,:cmd,:st,:queued_at) "
                       "RETURNING id",
                       -1, &insert_job_stmt, nullptr);

    auto cmd_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":cmd");

    auto st_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":st");

    auto queued_at_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":queued_at");

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

        bind_res = sqlite3_bind_int64(insert_job_stmt, queued_at_idx,
                                      Timestamp::now().as_s64());

        if (bind_res != SQLITE_OK) {
            logger->error("error binding queued_at {}", sqlite3_errmsg(dbCOnn));
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

    cout << "JUAN iniciado. PID: " << getpid() << endl;

    cout << "JUAN> " << flush;

    char buffer[1024];
    string entrada;
    string linea;

    struct pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {sigchld_pipe[0], POLLIN, 0}};

    while (true) {
        // Esperar hasta que haya un evento.
        int resultado = poll(fds, 2, -1);

        if (resultado == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll");
            break;
        }

        // Evento 1: terminó o cambió el estado de un job.
        if (fds[1].revents & POLLIN) {
            char eventos[128];

            // Vaciar las notificaciones pendientes.
            while (read(sigchld_pipe[0], eventos, sizeof(eventos)) > 0) {
            }

            // Consultar los procesos que realmente terminaron.
            actualizarJobs(dbCOnn, logger);
        }

        // Evento 2: hay entrada en la terminal.
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            ssize_t r = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);

            if (r > 0) {
                buffer[r] = '\0';
                entrada += buffer;

                size_t posicion;

                while ((posicion = entrada.find('\n')) != string::npos) {
                    linea = entrada.substr(0, posicion);
                    entrada.erase(0, posicion + 1);

                    logger->info("Got commandline: \"{}\"", linea);

                    if (linea == "salir" || linea == "exit") {

                        cout << "\nCerrando JUAN..." << endl;

                        for (auto& job : RuningJobs) {
                            auto mapaIt = mapaIdPid.find(job.id);
                            if (mapaIt == mapaIdPid.end()) {
                                continue;
                            }

                            pid_t pid = mapaIt->second;
                            int estado = 0;
                            cout << "Esperando PID " << pid << "..." << endl;

                            pid_t resultado;
                            do {
                                resultado = waitpid(pid, &estado, 0);
                            } while (resultado == -1 && errno == EINTR);

                            if (resultado == pid) {
                                registrarResultadoProceso(job, estado, dbCOnn, logger);
                            } else {
                                logger->error("Error esperando el proceso {}: {}", pid, errno);
                            }
                        }

                        return 0;
                    }

                    else if (linea == "ayuda" || linea == "help") {

                        cout << "Comandos disponibles:" << endl;

                        cout << "  ayuda          - Mostrar ayuda" << endl;

                        cout << "  status         - Mostrar estados" << endl;

                        cout << "  correr         - Ejecutar comando Linux" << endl;

                        cout << "  cancelar <id>  - cancela un proceso en ejecucion" << endl;

                        cout << "  corriendo      - Mostrar todos los procesos activos" << endl;

                        cout << "  cola           - Mostrar todos los procesos en la cola" << endl;

                        cout << "  jobs           - Mostrar todos los procesos" << endl;

                        cout << "  salir          - Cerrar JUAN" << endl;
                    }

                    else if (linea == "status") {

                        actualizarJobs(dbCOnn, logger);

                        cout << "JUAN funcionando..." << endl;

                        cout << "Jobs activos: " << RuningJobs.size() << "/" << MAX_JOBS << endl;
                        cout << "Jobs en cola: " << trabajosEnCola.size() << "/" << MAX_COLA
                             << endl;
                    } else if (linea.rfind("cancelar ", 0) == 0) {

                        string idTexto = linea.substr(9);

                        if (idTexto.empty()) {

                            cout << "Debes indicar el ID del job." << endl;
                        }

                        else {

                            try {

                                JobID id = stoll(idTexto);
                                if (cancelarEnCola(id, dbCOnn, logger)) {
                                    cout << "Job " << id << " cancelado y eliminado de la cola."
                                         << endl;
                                } else {

                                    auto it = mapaIdPid.find(id);

                                    if (it == mapaIdPid.end()) {

                                        cout << "No existe un job con ID " << id << "." << endl;
                                    }

                                    else {

                                        pid_t pid = it->second;

                                        // Buscar el Job
                                        for (auto& job : RuningJobs) {

                                            if (job.id == id) {

                                                job.status = CANCELING;
                                                break;
                                            }
                                        }

                                        if (kill(pid, SIGTERM) == 0) {

                                            cout << "Cancelando job " << id << " (PID " << pid
                                                 << ")..." << endl;

                                        } else {

                                            logger->error("error al matar proceso");
                                        }
                                    }
                                }
                            } catch (...) {

                                cout << "ID invalido." << endl;
                            }
                        }
                    } else if (linea == "corriendo") {
                        actualizarJobs(dbCOnn, logger);

                        if (RuningJobs.empty()) {

                            cout << "No hay jobs ejecutandose." << endl;

                        } else {

                            cout << "\nJobs activos:" << endl;

                            for (const auto& job : RuningJobs) {

                                cout << "ID: " << job.id << " | " << "PID: " << mapaIdPid[job.id]
                                     << " | " << "Comando: " << job.command << " | "
                                     << "Estado: " << Jobs_status_str(job.status) << endl;
                            }
                        }
                    } else if (linea == "cola") {
                        actualizarJobs(dbCOnn, logger);
                        if (trabajosEnCola.empty()) {
                            cout << "No hay Jobs en cola" << endl;
                        } else {
                            cout << "\nJobs en cola:" << endl;
                            auto auxiliar = trabajosEnCola;

                            while (!auxiliar.empty()) {
                                const auto& job = auxiliar.front();

                                cout << "ID: " << job.id << " | " << "PID: " << mapaIdPid[job.id]
                                     << " | " << "Comando: " << job.command << " | "
                                     << "Estado: " << Jobs_status_str(job.status) << endl;

                                auxiliar.pop();
                            }
                        }
                    }

                    else if (linea == "jobs") {

                        actualizarJobs(dbCOnn, logger);

                        if (RuningJobs.empty()) {

                            cout << "No hay jobs ejecutandose." << endl;

                        } else {

                            cout << "\nJobs activos:" << endl;

                            for (const auto& job : RuningJobs) {

                                cout << "ID: " << job.id << " | " << "PID: " << mapaIdPid[job.id]
                                     << " | " << "Comando: " << job.command << " | "
                                     << "Estado: " << Jobs_status_str(job.status) << endl;
                            }
                            if (trabajosEnCola.empty()) {
                                cout << "No hay Jobs en cola" << endl;
                            } else {
                                cout << "\nJobs en cola:" << endl;
                                auto auxiliar = trabajosEnCola;

                                while (!auxiliar.empty()) {
                                    const auto& job = auxiliar.front();

                                    cout << "ID: " << job.id << " | "
                                         << "PID: " << mapaIdPid[job.id] << " | "
                                         << "Comando: " << job.command << " | "
                                         << "Estado: " << Jobs_status_str(job.status) << endl;

                                    auxiliar.pop();
                                }
                            }
                        }
                    }

                    else if (linea.rfind("correr ", 0) == 0) {

                        string comando = linea.substr(7);

                        if (comando.empty()) {

                            cout << "Debes escribir un comando." << endl;

                        } else if (trabajosEnCola.size() >= MAX_COLA) {
                            cout << "Limite en cola de " << MAX_COLA << " jobs alcanzado." << endl;
                        } else if (RuningJobs.size() >= MAX_JOBS &&
                                   trabajosEnCola.size() < MAX_COLA) {

                            JobID id = get_id(comando, QUEUED);
                            Job nuevoJob;

                            nuevoJob.id = id;
                            nuevoJob.command = comando;
                            nuevoJob.status = QUEUED;

                            trabajosEnCola.push(nuevoJob);
                            cout << "Job creado en cola." << endl;

                            cout << "job id: " << id << endl;

                            cout << "Jobs activos: " << RuningJobs.size() << "/" << MAX_JOBS
                                 << endl;
                            cout << "posicion en cola: " << trabajosEnCola.size() << "/" << MAX_COLA
                                 << endl;

                        }

                        else {

                            JobID id = get_id(comando, QUEUED);
                            Job nuevoJob;

                            nuevoJob.id = id;
                            nuevoJob.command = comando;
                            nuevoJob.status = QUEUED;
                            correr(nuevoJob, dbCOnn, logger);
                        }
                    }

                    else if (linea == "correr") {

                        cout << "Debes escribir un comando " << "despues de 'correr'." << endl;
                    }

                    else if (linea.empty()) {
                    }

                    else {

                        cout << "Comando invalido" << endl;
                    }

                    cout << "JUAN> " << flush;
                }
            } else if (r == 0) {
                // La entrada estándar se cerró.
                break;
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("read");
                break;
            }
        }

        // Detectar errores en los descriptores.
        if ((fds[0].revents & (POLLERR | POLLNVAL)) ||
            (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            logger->info("Error en un descriptor de poll().");
            break;
        }
    }

    close(sigchld_pipe[0]);
    close(sigchld_pipe[1]);

    return 0;
}
