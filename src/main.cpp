#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>

using namespace std;

int main(int argc, char* argv[]) {

    cout << "JUAN iniciado. PID: " << getpid() << endl;

    // --------------------------------
    // Modo: ./juan comando argumentos
    // --------------------------------
    if (argc > 1) {

        string comando;

        for (int i = 1; i < argc; i++) {
            comando += argv[i];

            if (i < argc - 1)
                comando += " ";
        }

        cout << "Comando recibido: " << comando << endl;

        
    }

    // --------------------------------
    // Modo interactivo: ./juan
    // --------------------------------

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

    return 0;
}