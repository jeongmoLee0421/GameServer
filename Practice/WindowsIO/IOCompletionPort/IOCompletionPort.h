#pragma once

#include <WinSock2.h>

// 2023 08 16 이정모 home

// Overlapped IO 작업이 완료되면,
// 완료된 작업이 IOCP queue에 들어가고
// 우리는 queue에서 완료된 작업에 대한 정보를 얻어서
// 후처리를 하는
// IOCP 모델

// constexpr을 사용해 명확하게 컴파일 타임에 상수를 정의하고
// 해당 상수를 컴파일 타임부터 사용하겠다는 의미
constexpr int MAX_SOCKBUF = 1024;
constexpr int MAX_CLIENT = 100;
constexpr int MAX_WORKERTHREAD = 4;

struct ClientInfo;

class IOCompletionPort
{
public:
	IOCompletionPort();
	~IOCompletionPort();

public:
	bool InitSocket();
	void CloseSocket(ClientInfo* pClientInfo, bool isForce = false);

public:
	bool BindAndListen(int bindPort);
	bool StartServer();

	// IOCP queue에 완료된 Overlapped IO 작업이 없을 때
	// Waiting Thread Queue에 들어가서 대기할 thread 생성
	bool CreateWorkerThread();
	bool CreateAccepterThread();
	ClientInfo* GetEmptyClientInfo();

public:
	// 생성된 IOCP 객체에 socket과 식별가능한 key를 연결
	bool BindIOCompletionPort(ClientInfo* pClientInfo);

	// client가 새로 접속해서 OS에 메시지 수신 요청
	// 메시지 송신이 완료되어 다음 메시지 수신을 위해 OS에 메시지 수신 요청
	bool BindRecv(ClientInfo* pClientInfo);

	// 메시지 수신이 완료되어 echo하기 위해 OS에 송신 요청
	bool SendMsg(ClientInfo* pClientInfo, char* pMessage, int length);

	// 완료된 Overlapped IO 작업을 IOCP queue에서 꺼내고
	// 후 처리한다.
	void WorkerThread();
	void AccepterThread();

	void DestroyThread();

private:
	ClientInfo* mClientInfo;
	SOCKET mListenSocket;
	int mClientCount;

	// Waiting Thread Queue에 들어갈 thread
	HANDLE mWorkerThread[MAX_WORKERTHREAD];
	HANDLE mAccepterThread;
	HANDLE mIOCP;

	bool mIsWorkerRun;
	bool mIsAccepterRun;

	wchar_t mErrorMessage[1024];
};