#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <fcntl.h>

using namespace std;

const int MAX_JOBS = 10;

struct Job {
    pid_t pid;
    string comando;
};

vector<Job> jobs;


// Revisa si alguno de los procesos terminó
void actualizarJobs() {

    for (auto it = jobs.begin(); it != jobs.end(); ) {

        int estado;

        pid_t resultado = waitpid(it->pid, &estado, WNOHANG);

        if (resultado == 0) {
            // El proceso sigue ejecutándose
            ++it;
        }
        else if (resultado == it->pid) {

            cout << "\n[JOB TERMINADO] PID: "
                 << it->pid << endl;

            if (WIFEXITED(estado)) {
                cout << "Comando: " << it->comando << endl;
                cout << "Codigo: "
                     << WEXITSTATUS(estado) << endl;
            }

            it = jobs.erase(it);
        }
        else {
            ++it;
        }
    }
}


int main() {

    cout << "JUAN iniciado. PID: "
         << getpid() << endl;

	string linea;

	while (true) {

        // Revisar procesos terminados
        actualizarJobs();

        cout << "JUAN> ";
        getline(cin, linea);

        if (linea == "salir" || linea == "exit") {

            cout << "Cerrando JUAN..." << endl;

            // Esperar a que terminen los jobs
            for (auto &job : jobs) {

                cout << "Esperando PID "
                     << job.pid << "..." << endl;

                waitpid(job.pid, nullptr, 0);
            }

            break;
        }


        if (linea == "ayuda" || linea == "help") {

            cout << "Comandos disponibles:" << endl;
            cout << "  ayuda   - Mostrar ayuda" << endl;
            cout << "  status  - Mostrar estado" << endl;
            cout << "  correr  - Ejecutar comando Linux" << endl;
            cout << "  jobs    - Mostrar procesos" << endl;
            cout << "  salir   - Cerrar JUAN" << endl;

            continue;
        }


        if (linea == "status") {

            cout << "JUAN funcionando..." << endl;

            cout << "Jobs activos: "
                 << jobs.size()
                 << "/"
                 << MAX_JOBS
                 << endl;

            continue;
        }


        if (linea == "jobs") {

            actualizarJobs();

            if (jobs.empty()) {

                cout << "No hay jobs ejecutandose."
                     << endl;

            } else {

                cout << "\nJobs activos:" << endl;

                for (const auto &job : jobs) {

                    cout << "PID: "
                         << job.pid
                         << " | "
                         << job.comando
                         << endl;
                }
            }

            continue;
        }


        if (linea.rfind("correr ", 0) == 0) {

            string comando = linea.substr(7);

            if (comando.empty()) {

                cout << "Debes escribir un comando."
                     << endl;

                continue;
            }


            // Actualizar antes de crear otro proceso
            actualizarJobs();


            // Limitar a 10 procesos
            if (jobs.size() >= MAX_JOBS) {

                cout << "Limite de "
                     << MAX_JOBS
                     << " jobs alcanzado."
                     << endl;

                continue;
            }


            pid_t child = fork();


            if (child == -1) {

                cout << "Error al crear el proceso."
                     << endl;

                continue;
            }


            if (child == 0) {

                int salida = open(
                    "job_output.log",
                    O_WRONLY | O_CREAT | O_APPEND,
                    0644
                );

                if (salida == -1) {
                    perror("open");
                    _exit(1);
                }

                dup2(salida, STDOUT_FILENO);
                dup2(salida, STDERR_FILENO);

                close(salida);

                execl(
                    "/bin/sh",
                    "sh",
                    "-c",
                    comando.c_str(),
                    nullptr
                );

                _exit(127);
            }


            // =====================
            // PROCESO PADRE
            // =====================

            Job nuevoJob;

            nuevoJob.pid = child;
            nuevoJob.comando = comando;

            jobs.push_back(nuevoJob);


            cout << "Job creado." << endl;
            cout << "PID: " << child << endl;
            cout << "Jobs activos: "
                 << jobs.size()
                 << "/"
                 << MAX_JOBS
                 << endl;


            // IMPORTANTE:
            // NO hacemos waitpid(..., 0)
            // El padre continúa inmediatamente.

            continue;
        }


        if (linea == "correr") {

            cout << "Debes escribir un comando "
                    "despues de 'correr'."
                 << endl;

            continue;
        }


        if (linea.empty()) {
            continue;
        }


        cout << "Comando invalido"
             << endl;
    }


    return 0;
}