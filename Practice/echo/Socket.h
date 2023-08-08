#pragma once

// 2023 08 08 이정모 home
// 
// socket 생성 및 삭제.
// client socket과 server socket에 모두 대응되는 socket class.

#include <WinSock2.h>

class Socket
{
public:
	Socket();
	~Socket();

	// server, client 공통 함수
	bool InitSocket();
	void CloseSocket(SOCKET closeSocket, bool isForce);

	// server 전용 함수
	bool BindAndListen(int bindPort);
	bool StartServer();

	// client 전용 함수
	bool Connect(const char* ip, int port);

private:
	// client 입장에서는 server와 통신할 socket
	// server 입장에서는 client의 접속 요청을 기다리는 socket
	SOCKET mSocket;

	// server가 client의 연결 요청을 성공적으로 받아서
	// 해당 client와 전용으로 통신하기 위한 socket
	SOCKET mConnectedSocket;

	char mSocketBuf[1024];
};