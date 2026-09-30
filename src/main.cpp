#include "database.h"
#include "result.h"
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include <logger.h>


using namespace std;

const int MAX_JOBS = 10;

struct Job {
	pid_t pid;
	string comando;
};

vector<Job> jobs;

// Revisa si alguno de los procesos terminó
void actualizarJobs() {

	for (auto it = jobs.begin(); it != jobs.end();) {

		int estado;

		pid_t resultado = waitpid(it->pid, &estado, WNOHANG);

		if (resultado == 0) {
			// El proceso sigue ejecutándose
			++it;
		} else if (resultado == it->pid) {

			cout << "\n[JOB TERMINADO] PID: " << it->pid << endl;

			if (WIFEXITED(estado)) {
				cout << "Comando: " << it->comando << endl;
				cout << "Codigo: " << WEXITSTATUS(estado) << endl;
			}

			it = jobs.erase(it);
		} else {
			++it;
		}
	}
}

int main() {
	using namespace juan;
	auto logger_res = Logger::open("juan_log", LogLevel::ERROR);

	logger_res->error("Test error");
	logger_res->warn("Test warn");
	logger_res->info("Test info");
	logger_res->debug("Test debug");

	auto init_res = init_db("data.db");
	
	if(!init_res) {
		cerr << status_str(init_res.error()) << endl;
		return -1;
	}


	//sqlite3_open("data.db", &db);

	//sqlite3_stmt* stmt;
	//sqlite3_prepare_v2(db, "CREATE TABLE IF NOT EXISTS jobs (id INTEGER PRIMARY KEY, command TEXT NOT NULL, status INTEGER NOT NULL, queued_at INTEGER, launched_at INTEGER, finished_at INTEGER, result INTEGER ) STRICT", -1, &stmt, nullptr);
	//sqlite3_step(stmt);
	//sqlite3_finalize(stmt);

	//sqlite3_stmt* insert_job_stmt;
	//sqlite3_prepare_v2(db, "INSERT INTO jobs (id) VALUES (NULL) RETURNING id", -1, &insert_job_stmt, nullptr);

	auto get_id = [&]() {
		//auto ret = sqlite3_step(insert_job_stmt);
		//int64_t id = -1L;
		//if (ret == SQLITE_ROW) {
			//id = sqlite3_column_int64(insert_job_stmt, 0);
		//}
		//ret = sqlite3_step(insert_job_stmt);
		//if (ret == SQLITE_DONE) {
			//sqlite3_reset(insert_job_stmt);
			//return id;
		//}
		return -1L;		
	};



	cout << "JUAN iniciado. PID: " << getpid() << endl;

	string linea;

	cout << "JUAN> " << flush;
	while (getline(cin, linea)) {
		// Revisar procesos terminados
		actualizarJobs();


		if (linea == "salir" || linea == "exit") {

			cout << "Cerrando JUAN..." << endl;

			// Esperar a que terminen los jobs
			for (auto& job : jobs) {

				cout << "Esperando PID " << job.pid << "..." << endl;

				waitpid(job.pid, nullptr, 0);
			}

			break;
		} else if (linea == "ayuda" || linea == "help") {

			cout << "Comandos disponibles:" << endl;
			cout << "  ayuda   - Mostrar ayuda" << endl;
			cout << "  status  - Mostrar estado" << endl;
			cout << "  correr  - Ejecutar comando Linux" << endl;
			cout << "  jobs    - Mostrar procesos" << endl;
			cout << "  salir   - Cerrar JUAN" << endl;

		} else if (linea == "status") {

			cout << "JUAN funcionando..." << endl;

			cout << "Jobs activos: " << jobs.size() << "/" << MAX_JOBS << endl;

		} else if (linea == "jobs") {

			actualizarJobs();

			if (jobs.empty()) {

				cout << "No hay jobs ejecutandose." << endl;

			} else {

				cout << "\nJobs activos:" << endl;

				for (const auto& job : jobs) {

					cout << "PID: " << job.pid << " | " << job.comando << endl;
				}
			}

		} else if (linea.rfind("correr ", 0) == 0) {

			string comando = linea.substr(7);

			if (comando.empty()) {

				cout << "Debes escribir un comando." << endl;

				continue;
			}

			// Actualizar antes de crear otro proceso
			actualizarJobs();

			// Limitar a 10 procesos
			if (jobs.size() >= MAX_JOBS) {

				cout << "Limite de " << MAX_JOBS << " jobs alcanzado." << endl;
			} else {


			pid_t child = fork();

			if (child == -1) {

				cout << "Error al crear el proceso." << endl;

				continue;
			}

			if (child == 0) {

				int salida =
				    open("job_output.log", O_WRONLY | O_CREAT | O_APPEND, 0644);

				if (salida == -1) {
					perror("open");
					_exit(1);
				}

				dup2(salida, STDOUT_FILENO);
				dup2(salida, STDERR_FILENO);

				close(salida);

				execl("/bin/sh", "sh", "-c", comando.c_str(), nullptr);

				_exit(127);
			}

			// =====================
			// PROCESO PADRE
			// =====================

			Job nuevoJob;

			nuevoJob.pid     = child;
			nuevoJob.comando = comando;

			jobs.push_back(nuevoJob);

			cout << "Job creado." << endl;
			cout << "PID: " << child << endl;
			cout << "job id: " << get_id() << endl;
			cout << "Jobs activos: " << jobs.size() << "/" << MAX_JOBS << endl;

			// IMPORTANTE:
			// NO hacemos waitpid(..., 0)
			// El padre continúa inmediatamente.
			}
		} else if (linea == "correr") {

			cout << "Debes escribir un comando "
			        "despues de 'correr'."
			     << endl;

		} else if (linea.empty()) {
		} else {
			cout << "Comando invalido" << endl;
		}

		cout << "JUAN> " << flush;
	}

	return 0;
}
