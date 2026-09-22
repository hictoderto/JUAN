#include <iostream>
#include <unistd.h>

using namespace std;

int main() {
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
}
