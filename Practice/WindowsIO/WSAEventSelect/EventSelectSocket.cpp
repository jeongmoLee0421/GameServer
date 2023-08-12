#include <process.h>
#include <WinSock2.h>

#include "EventSelectSocket.h"

#pragma comment(lib, "ws2_32")

// 스레드 생성할 때 멤버 함수를 넣지 못하는 이유로
// 1. this 포인터를 레지스터에 넣고 스택 메모리에 push 하는 과정의 구현이 복잡하다던가
// 2. this를 전달할 수 있다고 해도 병렬적으로 처리되는 thread 작업에서
// 외부에서 해당 객체가 delete되면 thread 작업에 문제가 생길 수도 있기 때문이라고 생각된다.

// 해결책으로는
// 1. this 포인터를 함수 매개변수로 넘겨주는 방법(원본)
// 2. 값 복사 캡쳐를 사용한 람다(복사본)
// 3. bind()를 사용한 this 캡쳐(복사본)

// 스레드 생성할 때 작업할 함수로 멤버 함수를 넣을 수 없기 때문에
// 한번 감싼 전역함수를 만들고 인자로 event select를 void*로 바꿔서 넣어준다.
// 그리고 내부에서 캐스팅해서 실제 원하는 함수를 호출
unsigned int WINAPI CallWorkerThread(LPVOID p)
{
	EventSelectSocket* pEventSelectSocket = reinterpret_cast<EventSelectSocket*>(p);

	// this를 매개변수로 받아와서 호출은 가능하다.
	// 다만 이 thread가 작업을 마치기 전까지는
	// pEventSelect 객체를 메모리에서 해제하면 안된다.
	pEventSelectSocket->WorkerThread();
	return 0;
}

EventSelectSocket::EventSelectSocket()
	: mClientCount{ 0 }
	, mWorkerThread{ nullptr }
	, mIsWorkerRun{ false }
	, mSocketBuffer{ 0, }
	, mErrorMessage{ 0, }
{
	for (int i = 0; i < WSA_MAXIMUM_WAIT_EVENTS; ++i)
	{
		mClientInfo.mClientSocket[i] = INVALID_SOCKET;

		// 수동 리셋 모드, non-signaled 상태 event 객체 생성
		mClientInfo.mEventHandle[i] = WSACreateEvent();
	}
}

EventSelectSocket::~EventSelectSocket()
{
	// 생성한 event 객체를 닫아줌으로써
	// 운영체제가 event 커널 오브젝트를 정리하도록 하자
	for (int i = 0; i < WSA_MAXIMUM_WAIT_EVENTS; ++i)
	{
		WSACloseEvent(mClientInfo.mEventHandle[i]);
	}

	WSACleanup();
}

bool EventSelectSocket::InitSocket()
{
	WSADATA wsaData;
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (0 != ret)
	{
		wsprintf(
			mErrorMessage,
			L"WSAStartup() 함수 실패: %d",
			ret
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// 여기서 overlapped IO로 소켓을 생성한 이유를 모르겠음.
	// 그리고 server socket을 overlapped IO로 생성했다고 해서
	// accpet 되는 client socket이 자동으로 overlapped IO가 되는 것은 아님.
	// 필요하다면 client socket에 직접 세팅을 해야하는 것 같다.
	mClientInfo.mClientSocket[0] = WSASocket(
		AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		NULL,
		NULL,
		WSA_FLAG_OVERLAPPED
	);
	if (INVALID_SOCKET == mClientInfo.mClientSocket[0])
	{
		wsprintf(
			mErrorMessage,
			L"WSASocket() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

bool EventSelectSocket::CloseSocket(SOCKET closeSocket, bool isForce)
{
	struct linger _linger { 0, 0 };

	if (true == isForce)
	{
		_linger.l_onoff = 1;
	}

	shutdown(closeSocket, SD_BOTH);

	setsockopt(
		closeSocket,
		SOL_SOCKET,
		SO_LINGER,
		reinterpret_cast<const char*>(&_linger),
		sizeof(_linger)
	);

	closesocket(closeSocket);

	closeSocket = INVALID_SOCKET;

	return true;
}

bool EventSelectSocket::BindAndListen(int bindPort)
{
	SOCKADDR_IN serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(bindPort);

	// 0번 index가 server socket
	int ret = bind(
		mClientInfo.mClientSocket[0],
		reinterpret_cast<const sockaddr*>(&serverAddr),
		sizeof(serverAddr)
	);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"bind() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// 지정한 socket에
	// ACCEPT 또는 CLOSE 네트워크 이벤트가 발생하면
	// 지정한 핸들을 signaled 상태로 변경
	ret = WSAEventSelect(
		mClientInfo.mClientSocket[0],
		mClientInfo.mEventHandle[0],
		FD_ACCEPT | FD_CLOSE
	);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"WSAEventSelect() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// listen() 함수가 호출되면
	// 대기 큐를 생성해서 connect()를 요청한 client를 대기시키는데
	// 대기하는 client가 있다는 것이 accpet()할 준비가 되었다는 뜻이기 때문에
	// 대기하는 client가 있다면 아마도 FD_ACCEPT 네트워크 이벤트가 발생할 것이다.
	// 그렇기 때문에 WSAEventSelect()를 먼저 호출하고
	// 이후에 listen()을 호출해서 대기 큐에 client 요청을 받으면,
	// FD_ACCEPT 네트워크 이벤트를 감지해서
	// 해당하는 이벤트 객체가 signaled 상태가 될 것이다.
	ret = listen(mClientInfo.mClientSocket[0], 5);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"listen() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

bool EventSelectSocket::StartServer()
{
	bool ret = CreateWorkerThread();
	if (false == ret)
	{
		return false;
	}

	return true;
}

bool EventSelectSocket::CreateWorkerThread()
{
	unsigned int threadID{ 0 };

	mIsWorkerRun = true;
	mWorkerThread = reinterpret_cast<HANDLE>(
		_beginthreadex(
			nullptr,
			0,
			CallWorkerThread,
			this, // 스레드 내부에서 멤버 함수를 호출하기 위한 this 포인터
			CREATE_SUSPENDED, // 추후에 원하는 타이밍에 thread를 시작하겠다.
			&threadID)
		);

	if (NULL == mWorkerThread)
	{
		wsprintf(
			mErrorMessage,
			L"_beginthreadex() 함수 실패: %d",
			GetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		mIsWorkerRun = false;
		return false;
	}

	ResumeThread(mWorkerThread);
	return true;
}

int EventSelectSocket::GetEmptyIndex()
{
	// 0번 index는 listen socket
	for (int i = 1; i < WSA_MAXIMUM_WAIT_EVENTS; ++i)
	{
		if (INVALID_SOCKET == mClientInfo.mClientSocket[i])
		{
			return i;
		}
	}

	return -1;
}

void EventSelectSocket::WorkerThread()
{
	WSANETWORKEVENTS wsaNetworkEvent{};

	while (mIsWorkerRun)
	{
		// 관리하는 event 객체들 중에
		// signaled 상태가 있으면 반환
		DWORD objIndex = WSAWaitForMultipleEvents(
			WSA_MAXIMUM_WAIT_EVENTS,
			mClientInfo.mEventHandle,
			false, // 하나의 event 객체가 signaled 상태가 되더라도 반환
			INFINITE,
			false
		);
		if (WSA_WAIT_FAILED == objIndex)
		{
			wsprintf(
				mErrorMessage,
				L"WSAWaitForMultipleEvents() 함수 실패: %d",
				WSAGetLastError()
			);

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			break;
		}

		// (WSA_WAIT_EVENT_0 + cEvents - 1)가
		// WSAWaitForMultipleEvents()에서 반환되고
		// WSA_WAIT_EVENT_0 는 0으로 정의되어 있기 때문에
		// signaled 상태로 변경한 event 객체의 index는 반환 값 objIndex
		int ret = WSAEnumNetworkEvents(
			mClientInfo.mClientSocket[objIndex],
			mClientInfo.mEventHandle[objIndex], // signaled 상태의 event 객체를 non-signaled 상태로 변경
			&wsaNetworkEvent
		);
		if (SOCKET_ERROR == ret)
		{
			wsprintf(
				mErrorMessage,
				L"WSAEnumNetworkEvents() 함수 실패: %d",
				WSAGetLastError()
			);

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			break;
		}

		// 다른 thread에서 DestroyThread()가 호출되면,
		// thread를 정지하라는 뜻이다.
		// mIsWorkerRun이 true여서 while문을 진입하였을지라도
		// 병렬적으로 실행되는 thread의 특성상
		// 외부에서 false로 값을 바꾸면
		// while문 실행 중간에 나올 수도 있어야 하기 떄문에 코드를 넣었다.
		if (false == mIsWorkerRun && 0 == objIndex)
		{
			break;
		}

		// if-else가 아니라
		// 독립적인 if로 처리한 이유는
		// 한 소켓에 네트워크 이벤트가 여러개가 발생할 수 있고
		// 그것들을 한번에 검사하려면 하나씩 다 검사해야 함
		if (wsaNetworkEvent.lNetworkEvents & FD_ACCEPT)
		{
			if (0 != wsaNetworkEvent.iErrorCode[FD_ACCEPT_BIT])
			{
				wsprintf(
					mErrorMessage,
					L"WSAEnumNetworkEvents() -> FD_ACCEPT 실패: %d",
					WSAGetLastError()
				);

				MessageBox(NULL,
					mErrorMessage,
					NULL,
					MB_OK);

				break;
			}

			DoAccept();
		}

		if (wsaNetworkEvent.lNetworkEvents & FD_READ)
		{
			if (0 != wsaNetworkEvent.iErrorCode[FD_READ_BIT])
			{
				wsprintf(
					mErrorMessage,
					L"WSAEnumNetworkEvents() -> FD_READ 실패: %d",
					WSAGetLastError()
				);

				MessageBox(NULL,
					mErrorMessage,
					NULL,
					MB_OK);

				break;
			}

			DoRecv(objIndex);
		}

		if (wsaNetworkEvent.lNetworkEvents & FD_CLOSE)
		{
			closesocket(mClientInfo.mClientSocket[objIndex]);
			mClientInfo.mClientSocket[objIndex] = INVALID_SOCKET;
		}
	}
}

void EventSelectSocket::DoAccept()
{
	SOCKADDR_IN clientAddr{};
	int addrLen = sizeof(clientAddr);

	// client 정보에서 비어 있는 배열 위치 가져오자
	int index = GetEmptyIndex();
	if (-1 == index)
	{
		wsprintf(
			mErrorMessage,
			L"더 이상 client를 받을 수 없습니다."
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return;
	}

	// FD_ACCEPT 네트워크 이벤트가 발생했다는게
	// listen() 호출 후에 생성된 대기 큐에 client 요청이 들어왔다는 의미
	// 비어 있는 위치인 index에 새로운 client socket을 저장하자
	mClientInfo.mClientSocket[index] = accept(
		mClientInfo.mClientSocket[0],
		reinterpret_cast<sockaddr*>(&clientAddr),
		&addrLen
	);

	if (INVALID_SOCKET == mClientInfo.mClientSocket[index])
	{
		wsprintf(
			mErrorMessage,
			L"accept() 함수 에러: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return;
	}

	// client와 통신하기 위한 전용 client socket도
	// 네트워크 이벤트가 발생여부를 감지하라고 등록
	int ret = WSAEventSelect(
		mClientInfo.mClientSocket[index],
		mClientInfo.mEventHandle[index],
		FD_READ | FD_CLOSE
	);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"WSAEventSelect() 함수 에러: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return;
	}

	++mClientCount;
}

void EventSelectSocket::DoRecv(DWORD objIdx)
{
	// tcp는 네트워크 상황에 따라 패킷 전송이 느려질 수 있는데
	// 내가 만약 4바이트를 받아야 하는 상황에서
	// OS의 수신 버퍼에 1바이트만 있다면
	// recv() 호출 시 1바이트만 받아오게 된다.
	// 그렇기 때문에 나머지 3바이트는 반복문을 돌면서
	// 끝까지 다 받아내야 한다.
	int recvLen = recv(
		mClientInfo.mClientSocket[objIdx],
		mSocketBuffer,
		sizeof(mSocketBuffer),
		0
	);
	if (0 == recvLen)
	{
		wsprintf(
			mErrorMessage,
			L"client와 연결이 종료되었습니다."
		);

		CloseSocket(mClientInfo.mClientSocket[objIdx]);
		--mClientCount;

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return;
	}
	else if (SOCKET_ERROR == recvLen)
	{
		wsprintf(
			mErrorMessage,
			L"recv() 함수 에러: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);
		return;
	}

	// recvLen바이트를 받고
	// recvLen바이트를 전송할 것이기 때문에
	// null 문자 처리는 하지 않아도 되긴하다.
	mSocketBuffer[recvLen] = '\0';

	int sendLen = send(
		mClientInfo.mClientSocket[objIdx],
		mSocketBuffer,
		recvLen,
		0
	);
	if (SOCKET_ERROR == sendLen)
	{
		wsprintf(
			mErrorMessage,
			L"send() 함수 에러: %d",
			WSAGetLastError()
		);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);
		return;
	}
}

void EventSelectSocket::DestroyThread()
{
	mIsWorkerRun = false;

	// server socket도 닫아주자
	closesocket(mClientInfo.mClientSocket[0]);

	// 위에서 closesocket()을 호출하면
	// FD_CLOSE 네트워크 이벤트를 감지하면서
	// event 객체 상태를 signaled 상태로 변경할 것이고
	// 그렇다면 while문에서 탈출할 것이라고 생각했는데
	// 
	// 예제에서는 SetEvent()를 직접 호출해서 signaled 상태로 변경했다.
	SetEvent(mClientInfo.mEventHandle[0]);

	WaitForSingleObject(mWorkerThread, INFINITE);
}
