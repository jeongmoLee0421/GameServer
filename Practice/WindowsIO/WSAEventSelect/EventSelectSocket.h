#pragma once

// 2023 08 12 이정모 home

// WSAEventSelect()를 사용한 소켓

#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>

// client 정보를 담기위한 구조체
// 참고로 0번은 접속(ACCEPT)을 처리하기 위한 server socket이다.
// server socket이 접속을 수락하면 새로운 client socket이 생기고
// 이 client socket도 event를 감지할 수 있도록 WSAEventSelect()를 호출
struct ClientInfo
{
	// n번째 socket에 네트워크 이벤트가 감지되면
	// n번째 event 객체가 signaled 상태로 변경
	WSAEVENT mEventHandle[WSA_MAXIMUM_WAIT_EVENTS];
	SOCKET mClientSocket[WSA_MAXIMUM_WAIT_EVENTS];
};

class EventSelectSocket
{
public:
	EventSelectSocket();
	~EventSelectSocket();

public:
	bool InitSocket();
	bool CloseSocket(SOCKET closeSocket, bool isForce = false);

public:
	bool BindAndListen(int bindPort);
	bool StartServer();
	bool CreateWorkerThread();

	// server가 accpet를 해서
	// client와 통신하는 전용 socket을 생성하면
	// 해당 소켓을 저장하기 위해서 clientSocket 배열에서
	// 사용되지 않는 인덱스를 얻어오기 위함
	int GetEmptyIndex();

public:
	void WorkerThread();
	void DoAccept();
	void DoRecv(DWORD objIdx);
	void DestroyThread();

private:
	ClientInfo mClientInfo;

	// 접속된 클라이언트 수
	int mClientCount;

	HANDLE mWorkerThread;
	bool mIsWorkerRun;
	char mSocketBuffer[1024];

	wchar_t mErrorMessage[1024];
};