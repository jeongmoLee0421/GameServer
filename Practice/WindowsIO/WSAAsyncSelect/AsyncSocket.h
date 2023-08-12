#pragma once

// 2023 08 11 이정모 home

// WSAAsyncSelect()를 사용한 소켓

// _WINSOCKAPI_을 먼저 정의해서
// windows.h 내부에 있는 winsock.h 정보가 포함되지 않도록 했다.
// 만약 포함된다면 winsock2.h와 충돌이 발생
#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>

// socket message define을 .h에 넣어둠으로써
// 다른 파일이 .h를 참조할 때 이 define을 사용하도록 함
#define WM_SOCKETMSG WM_USER+1

class AsyncSocket
{
public:
	AsyncSocket();
	~AsyncSocket();

	bool InitSocket(HWND hWnd);

	void CloseSocket(SOCKET closeSocket, bool isForce = false);

	bool BindAndListen(int bindPort);

	bool StartServer();

private:
	SOCKET mListenSocket;

	// network event가 발생하면
	// 메시지를 넣을 window handle
	HWND mHwnd;
};