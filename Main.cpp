#include "Server.h"

int main(){
	Server *MyServer = new Server;
	if (MyServer->WinsockStartup() == -1) return -1;
	if (MyServer->ServerStartup() == -1) return -1;
	if (MyServer->ListenStartup() == -1) return -1;
	if (MyServer->Loop() == -1) return -1;
	return 0;
}