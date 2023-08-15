#pragma once

#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>

// 2023 08 14 이정모 home

// Callback 함수를 사용한 Overlapped IO
// event 기반 Overlapped IO에서 최대 이벤트 객체 수가 64개로
// 클라이언트도 64개까지만 수용할 수 있었으나
// Callback 기반 Overlapped IO는 그 이상 수용 가능
// 또 WaitForMultipleEvents(), GetOverlappedResult()를 호출하여
// 완료된 작업에 대한 결과를 얻어 처리하지 않을 수 있다.
// IO가 완료되면 OS가 직접 함수를 호출해주기 때문

constexpr int MAX_SOCKBUF = 1024;

enum class eOperation
{
	OP_RECV,
	OP_SEND,
};

struct OverlappedEx
{
	// WSAOVERLAPPED 구조체를 직접 사용하지는 않지만,
	// WSASend(), WSARecv() 함수에서 사용된다.
	// 그리고 CompletionRoutine()이 호출될 때
	// WSAOVERLAPPED 구조체의 포인터가 인자로 넘어오는데
	// 이걸 OverlappedEx 포인터로 형변환해서 다른 멤버 변수를 사용할 수 있다.
	WSAOVERLAPPED mWSAOverlapped;

	SOCKET mClientSocket;
	WSABUF mWSABuffer;
	char mBuffer[MAX_SOCKBUF];
	eOperation mOperation;

	// 수신이 끝나면 echo를 위해 SendMsg()를 호출해서
	// OS에 송신을 요청해야 하고
	// 송신이 끝나면 다시 수신을 위해 BindRecv()를 호출해서
	// OS에 수신을 요청해야 한다.
	// 이 때 위 2개의 멤버 함수를 호출하기 위해서
	// this 포인터가 필요한데 이 멤버 변수에 담는다.
	void* mClassPtr;
};

class OverlappedCallback
{
public:
	OverlappedCallback();
	~OverlappedCallback();

public:
	bool InitSocket(HWND hWnd);
	bool CloseSocket(SOCKET closeSocket, bool isForce = false);

public:
	bool BindAndListen(int bindPort);
	bool StartServer();

public:
	bool CreateAccepterThread();
	void AccepterThread();

public:
	// WSARecv() 호출을 통해 OS에 overlapped Input 요청
	bool BindRecv(SOCKET socket);

	// WSASend() 호출을 통해 OS에 overlapped Output 요청
	bool SendMsg(SOCKET socket, char* pMsg, int len);

	void DestroyThread();

private:
	// 메시지 박스를 띄울 윈도우 핸들
	HWND mHwnd;
	wchar_t mErrorMessage[1024];

	int mClientCount;

	HANDLE mAccepterThread;
	bool mIsAccepterRun;

	SOCKET mListenSocket;
};