#include <iostream>
#include <string.h>
#include "librtmp/librtmp.h"

#define CHECK_ARGID if (arg_id == argc) Error();
using namespace std;

uint16_t rtmp_port = 1935;

void Error() {
	cout << "Error parameter" << endl;
	exit(1);
}

void ParseCli(char* argv[], int& arg_id, int argc) {
	if (strcmp(argv[arg_id], "--help") == 0) {
		cout << "----------" << endl <<
			"Usage:" << endl <<
			"-p 1935	set rtmp port" << endl <<
			"q	shutdown" << endl << "----------" << endl;
		exit(0);
	}
	else if (strcmp(argv[arg_id], "-p") == 0) {
		arg_id++;
		CHECK_ARGID;
		rtmp_port = atoi(argv[arg_id]);
	}
}

int main(int argc, char* argv[]) {
	cout << "RTMP to NDI software. (c) 2021 Alexander Babansky" << endl << argv[0] << " --help	for usage" << endl << endl;;
	if (argc > 1) {
		for (int arg_id = 1; arg_id < argc; arg_id++) {
			ParseCli(argv, arg_id, argc);
		}
	}
	try {
		LibRTMP rtmp(rtmp_port);
		char c;
		cin >> c;
		cout << "Shutting down..." << endl;
	}
	catch (exception& e) {
		cout << "Error: " << e.what() << endl;
	}
	cout << "Bye!" << endl;
}