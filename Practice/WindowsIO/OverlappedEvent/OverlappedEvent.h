#pragma once

// 2023 08 13 이정모 home

// Overlapped IO에서 Event를 사용한 모델

#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>

constexpr int MAX_SOCKBUF = 1024;

// WSASend() 또는 WSARecv()를 통해서
// 중첩 IO를 수행하는데
// IO 완료 후 WSAGetOverlappedResult()에서
// 어떤 작업이 완료되었는지 구분할 수가 없어서 사용
enum class eOperation
{
	OP_RECV,
	OP_SEND,
};

// WSAOVERLAPPED 구조체를 포함해서
// 필요한 정보를 추가로 넣어서 확장한 구조체
struct OverlappedEx
{
	// WSARecv(), WSASend()에서 OVERLAPPED 구조체의 주소를 넣게 되어있다.
	// 구조체의 첫번째 멤버 변수의 주소는 구조체의 시작 주소와 같은데
	// WSARecv(), WSASend()에 OVERLAPPED*로 이 구조체를 캐스팅해서 넣어준다.
	// 운영체제는 OVERLAPPED 구조체에 데이터를 쓸 수 있고
	// 프로그래머는 OVERLAPPED 구조체 뒤에 포함시킨 추가적인 정보도 사용할 수 있다.
	WSAOVERLAPPED mWSAOverlapped;

	// ClientInfo 구조체 배열 인덱스
	int mIndex;

	// Overlapped IO가 작업할 버퍼의 개수 및 시작 주소
	WSABUF mWSABuf;

	// 실제 데이터가 저장되는 버퍼
	char mBuffer[MAX_SOCKBUF];

	// Overlapped IO 작업 종류의 구분
	// send? recv?
	eOperation mOperation;
};

// client 정보를 담기위한 구조체
// 예외로 0번 인덱스에 server 정보를 담을 것이다.
struct ClientInfo
{
	SOCKET mClientSocket[WSA_MAXIMUM_WAIT_EVENTS];
	WSAEVENT mEventHandle[WSA_MAXIMUM_WAIT_EVENTS];
	OverlappedEx mOverlappedEx[WSA_MAXIMUM_WAIT_EVENTS];
};

class OverlappedEvent
{
public:
	OverlappedEvent();
	~OverlappedEvent();

public:
	bool InitSocket(HWND hwnd);
	void CloseSocket(SOCKET closeSocket, bool isForce = false);

public:
	bool BindAndListen(int bindPort);
	bool StartServer();

public:
	// Overlapped IO 작업 처리를 위한 thread 생성
	bool CreateWorkerThread();

	// accept를 처리하는 thread 생성
	bool CreateAccepterThread();

	// ClientInfo 배열에서 사용되지 않는 공간 찾기
	int GetEmptyIndex();

	// WSARecv Overlapped IO 작업
	bool BindRecv(int index);
	
	// WSASend Overlapped IO 작업
	bool SendMsg(int index, char* pMsg, int length);

public:
	// Overlapped IO 작업에 대한 완료 통보를 받아서(WSAMultipleEvents())
	// OverlappedResult() 호출
	void WorkerThread();

	// client 접속 요청을 수락해서 전용 client socket을 만들고
	// 해당 socket이 데이터를 수신할 수 있도록
	// BindRecv() 함수를 호출해줌
	void AccepterThread();

	// Overlapped IO 작업이 완료되었기 때문에
	// 그에 대한 처리
	void OverlappedResult(int index);

	void DestroyThread();

private:
	ClientInfo mClientInfo;
	int mClientCount;

	HANDLE mWorkerThread;
	HANDLE mAccepterThread;

	bool mIsWorkerRun;
	bool mIsAccepterRun;

	// message box를 띄울 윈도우 핸들
	HWND mHwnd;
	wchar_t mErrorMessage[256];
};