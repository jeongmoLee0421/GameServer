// 2023 08 08 이정모 home
// 
// client가 메시지를 송신하면
// server가 받아서 그대로 다시 보내주는
// echo server

#include <iostream>

#include "Socket.h"

const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8000;

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "usage: filename.exe [option: [/server][/client]]" << std::endl;
		return 0;
	}

	Socket socket{};
	if (0 == _stricmp(argv[1], "/server"))
	{
		socket.InitSocket();
		socket.BindAndListen(SERVER_PORT);
		socket.StartServer();
	}
	else if (0 == _stricmp(argv[1], "/client"))
	{
		socket.InitSocket();
		socket.Connect(SERVER_IP, SERVER_PORT);
	}
	else
	{
		std::cout << "usage: filename.exe [option: [/server][/client]]" << std::endl;
		return 0;
	}

	return 0;
}