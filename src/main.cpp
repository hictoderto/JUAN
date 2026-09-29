#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sqlite3.h>

using namespace std;

int main() {

cout << "JUAN iniciando. PID: " << getpid() << endl;

	sqlite3* db;

	int res = sqlite3_open("data.db", &db);
	if(res != SQLITE_OK) {
		cout << "Unable to create database" << endl;
		return 1;
	}

	int pipefds[2];
	int returnstatus;

	returnstatus = pipe(pipefds);

	if (returnstatus == -1) {
		cout << "Unable to create pipe" << endl;
		return 1;
	}

	pid_t pid = getpid();
	cout << "my pid is: " << pid << endl;
	// 0 stdin
	// 1 stdout

	pid_t child = fork();
	if (child) {
		cout << "child pid is: " << child << endl;
		char c;
		while(read(pipefds[0], &c, 1) >= 0) {
			cout << c;
		}
	} else {
		dup2(pipefds[1], 1);
		char* args[] = {"/bin/python", "-c", "import os; print(os.getpid());",nullptr};
		char* envp[] = {nullptr};
		execv("/bin/python", args);
	}

	string linea;

	while (true) {

			cout << "JUAN> ";
			getline(cin, linea);

			if (linea == "salir" || linea == "exit") {
					cout << "Cerrando JUAN..." << endl;
					break;
			}

			if (linea == "ayuda" || linea == "help") {
					cout << "Comandos disponibles:" << endl;
					cout << "  ayuda   - Mostrar ayuda" << endl;
					cout << "  status  - Mostrar estado" << endl;
					cout << "  salir   - Cerrar JUAN" << endl;
					continue;
			}

			if (linea == "status") {
					cout << "JUAN funcionando..." << endl;
					continue;
			}

			if (linea.empty()) {
					continue;
			}

			cout << "Comando invalido " << endl;
	}


}
