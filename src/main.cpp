#include "database.h"
#include "job.h"
#include "result.h"

#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <signal.h>
#include <sqlite3.h>
#include <string>
#include <sys/wait.h>
#include <types.h>
#include <unistd.h>
#include <vector>

#include <juan_session.h>
#include <logger.h>

using namespace juan;
using namespace std;
const int MAX_JOBS = 10;

vector<Job> RuningJobs;
map<JobID, pid_t> mapaIdPid;

void actualizarJobs() {

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

				int codigo = WEXITSTATUS(estado);

				cout << "Codigo: " << codigo << endl;

				it->result = codigo;

				if (codigo == 0) {
					it->status = SUCCEDED;
				} else {
					it->status = FAILED;
				}

			} else if (WIFSIGNALED(estado)) {

				cout << "Terminado por senal: " << WTERMSIG(estado) << endl;

				if (it->status == CANCELING) {
					it->status = CANCELED;
				} else {
					it->status = FAILED;
				}
			}

			else if (WIFSIGNALED(estado)) {

				cout << "Terminado por senal: " << WTERMSIG(estado) << endl;

				it->status = CANCELED;
			}

			mapaIdPid.erase(mapaIt);

			it = RuningJobs.erase(it);

			cout << "JUAN> " << flush;
		}

		else {

			++it;
		}
	}
}

int main() {

	using namespace juan;

	auto logger_res = OFSOutput::open("juan_log");
	if (!logger_res) {
		std::cout << "Failed to open log file" << std::endl;
	}

	auto logger = std::make_shared<Logger>();
	if (logger_res)
		*logger = Logger(std::move(*logger_res), LogLevel::INFO);

	auto db = DB::connect("data.db", logger);
	if (!db) {
		logger->error("Failed to connect to db {}", status_str(db.error()));
		return -1;
	}

	auto session_res = JuanSession::init(logger, std::move(*db));
	if (!session_res) {
		logger->error("Failed to create session {}",
		              status_str(session_res.error()));
		return -1;
	}
	auto& juan_session = *session_res;
	logger->info("Started juan session with id: {}",
	             juan_session.get_session().id);

	sqlite3_stmt* stmt;

	sqlite3_prepare_v2(db->db,
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

	sqlite3_prepare_v2(db->db,
	                   "INSERT INTO jobs "
	                   "(id,command,status) "
	                   "VALUES (NULL,:cmd,:st) "
	                   "RETURNING id",
	                   -1, &insert_job_stmt, nullptr);

	auto cmd_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":cmd");

	auto st_idx = sqlite3_bind_parameter_index(insert_job_stmt, ":st");

	auto get_id = [&](const string& cmd) {
		int bind_res;

		bind_res =
		    sqlite3_bind_text64(insert_job_stmt, cmd_idx, cmd.c_str(),
		                        cmd.length(), SQLITE_TRANSIENT, SQLITE_UTF8);

		if (bind_res != SQLITE_OK) {

			logger->error("error binding cmd {}", sqlite3_errmsg(db->db));
		}

		bind_res = sqlite3_bind_int64(insert_job_stmt, st_idx, 1);

		if (bind_res != SQLITE_OK) {

			logger->error("error binding st {}", sqlite3_errmsg(db->db));
		}

		auto ret = sqlite3_step(insert_job_stmt);

		int64_t id = -1L;

		if (ret == SQLITE_ROW) {

			id = sqlite3_column_int64(insert_job_stmt, 0);

		} else {

			logger->error("error inserting {}", sqlite3_errmsg(db->db));
		}

		ret = sqlite3_step(insert_job_stmt);

		if (ret == SQLITE_DONE) {

			sqlite3_reset(insert_job_stmt);

			return id;
		}

		return -1L;
	};

	int flags = fcntl(STDIN_FILENO, F_GETFL, 0);

	if (flags == -1) {

		perror("fcntl F_GETFL");
		return 1;
	}

	if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) {

		perror("fcntl F_SETFL");
		return 1;
	}

	cout << "JUAN iniciado. PID: " << getpid() << endl;

	cout << "JUAN> " << flush;

	char buffer[1024];

	string entrada;

	string linea;

	while (true) {

		actualizarJobs();

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

						cout << "Esperando PID " << mapaIdPid[job.id] << "..."
						     << endl;

						waitpid(mapaIdPid[job.id], nullptr, 0);
					}

					return 0;
				}

				else if (linea == "ayuda" || linea == "help") {

					cout << "Comandos disponibles:" << endl;

					cout << "  ayuda   - Mostrar ayuda" << endl;

					cout << "  status  - Mostrar estados" << endl;

					cout << "  correr  - Ejecutar comando Linux" << endl;

					cout << "  cancelar <id>  - cancela un proceso en ejecucion"
					     << endl;

					cout << "  jobs    - Mostrar procesos" << endl;

					cout << "  salir   - Cerrar JUAN" << endl;
				}

				else if (linea == "status") {

					actualizarJobs();

					cout << "JUAN funcionando..." << endl;

					cout << "Jobs activos: " << RuningJobs.size() << "/"
					     << MAX_JOBS << endl;
				} else if (linea.rfind("cancelar ", 0) == 0) {

					string idTexto = linea.substr(9);

					if (idTexto.empty()) {

						cout << "Debes indicar el ID del job." << endl;
					}

					else {

						try {

							JobID id = stoll(idTexto);

							auto it = mapaIdPid.find(id);

							if (it == mapaIdPid.end()) {

								cout << "No existe un job con ID " << id << "."
								     << endl;
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

									cout << "Cancelando job " << id << " (PID "
									     << pid << ")..." << endl;

								} else {

									perror("kill");
								}
							}

						} catch (...) {

							cout << "ID invalido." << endl;
						}
					}
				}

				else if (linea == "jobs") {

					actualizarJobs();

					if (RuningJobs.empty()) {

						cout << "No hay jobs ejecutandose." << endl;

					} else {

						cout << "\nJobs activos:" << endl;

						for (const auto& job : RuningJobs) {

							cout << "ID: " << job.id << " | "
							     << "PID: " << mapaIdPid[job.id] << " | "
							     << "Comando: " << job.command << " | "
							     << "Estado: " << static_cast<int>(job.status)
							     << endl;
						}
					}
				}

				else if (linea.rfind("correr ", 0) == 0) {

					string comando = linea.substr(7);

					if (comando.empty()) {

						cout << "Debes escribir un comando." << endl;

					}

					else if (RuningJobs.size() >= MAX_JOBS) {

						cout << "Limite de " << MAX_JOBS << " jobs alcanzado."
						     << endl;

					}

					else {

                        JobID id = get_id(comando); //Capturamos el id antes de hacer fork para evitar problemas de concurrencia
						pid_t child = fork();

						if (child == -1) {

							cout << "Error al crear "
							     << "el proceso." << endl;

						}

						else if (child == 0) {
                            string logFileNameOut = "job_output_" + to_string(id) + ".log";
                            string logFileNameErr = "job_error_" + to_string(id) + ".log";

                            // Inicializamos el archivo de salida para stdout y stderr
							int salida_out = open(logFileNameOut.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
                            int salida_err = open(logFileNameErr.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

							if (salida_out == -1 || salida_err == -1) {

								perror("open");

                                if (salida_out != -1) close(salida_out);
                                if (salida_err != -1) close(salida_err);

								_exit(1);
							}

							dup2(salida_out, STDOUT_FILENO);
							dup2(salida_err, STDERR_FILENO);

							close(salida_out);
							close(salida_err);

							execl("/bin/sh", "sh", "-c", comando.c_str(),
							      nullptr);

							_exit(127);
						}

						else {

							Job nuevoJob;

							nuevoJob.id      = id;
							nuevoJob.command = comando;
							nuevoJob.status  = LAUNCHED;

							RuningJobs.push_back(nuevoJob);

							mapaIdPid[id] = child;

							cout << "Job creado." << endl;

							cout << "PID: " << child << endl;

							cout << "job id: " << id << endl;

							cout << "Jobs activos: " << RuningJobs.size() << "/"
							     << MAX_JOBS << endl;
						}
					}
				}

				else if (linea == "correr") {

					cout << "Debes escribir un comando "
					     << "despues de 'correr'." << endl;
				}

				else if (linea.empty()) {
				}

				else {

					cout << "Comando invalido" << endl;
				}

				cout << "JUAN> " << flush;
			}
		}

		else if (r == -1) {

			if (errno != EAGAIN && errno != EWOULDBLOCK) {

				perror("read");
				break;
			}

		}

		else if (r == 0) {

			break;
		}
	}

	return 0;
}
