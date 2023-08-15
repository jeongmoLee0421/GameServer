#include <WinSock2.h>
#include <process.h>
#include <cerrno>

#pragma comment(lib, "ws2_32")

#include "OverlappedCallback.h"

unsigned int WINAPI CallAccepterThread(LPVOID p)
{
	OverlappedCallback* pOverlappedCallback = reinterpret_cast<OverlappedCallback*>(p);

	pOverlappedCallback->AccepterThread();

	return 0;
}

// IO 작업이 완료되면
// OS가 직접 이 함수를 호출
void CALLBACK CompletionRoutine(DWORD error,
	DWORD transferred,
	LPWSAOVERLAPPED overlapped,
	DWORD flags);

OverlappedCallback::OverlappedCallback()
	: mHwnd{ NULL }
	, mClientCount{ 0 }
	, mAccepterThread{ NULL }
	, mIsAccepterRun{ false }
	, mListenSocket{ INVALID_SOCKET }
	, mErrorMessage{ 0, }
{
}

OverlappedCallback::~OverlappedCallback()
{
	WSACleanup();

	DestroyThread();
}

void OverlappedCallback::DestroyThread()
{
	mIsAccepterRun = false;

	// closesocket()이 호출되었는데
	// 해당 socket이 blocking 되어 있다면,
	// blocking 상태를 해제하며, 다음 코드를 실행한다.
	closesocket(mListenSocket);

	WaitForSingleObject(mAccepterThread, INFINITE);
}

bool OverlappedCallback::InitSocket(HWND hWnd)
{
	mHwnd = hWnd;

	WSADATA wsaData{};
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (0 != ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] WSAStartup(): %d",
			ret);

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// WSA_FLAG_OVERLAPPED flag를 통해 overlapped IO 소켓을 생성
	// overlapped IO를 사용한다는 것의 의미는
	// IO를 요청한 뒤 IO가 완료되지 않아도 다음 작업을 수행할 수 있고
	// 그 다음 작업마저 IO를 요청하는 즉, 중첩으로 IO를 요청한다는 것
	mListenSocket = WSASocket(
		AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		NULL,
		NULL,
		WSA_FLAG_OVERLAPPED
	);
	if (INVALID_SOCKET == mListenSocket)
	{
		wsprintf(mErrorMessage,
			L"[에러] WSASocket(): %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// server socket을 non-blocing 모드로 세팅하여
	// accept() 함수 호출시 client 접속 요청이 없더라도 반환하도록 함.
	// 그 이유는 while문 조건을 false로 세팅하여 thread를 종료하고 있는데
	// accept()에서 blocking 되면 while문 조건을 확인할 수가 없어서 종료하려면 강제 종료 해야하기 때문
	//u_long mode = 1;
	//ret = ioctlsocket(mListenSocket, FIONBIO, &mode);

	// thread를 안전하게 종료하기 위해서 listen socket을 non-blocking 모드로 세팅했는데
	// completion routine()이 올바르게 호출되지 않았음

	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] ioctlsocket(): %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

bool OverlappedCallback::CloseSocket(SOCKET closeSocket, bool isForce)
{
	struct linger lingerOption { 0, 0 };

	if (true == isForce)
	{
		lingerOption.l_onoff = 1;
	}

	// 이 쪽 코드에 문제가 있음(해결 못함)
	// 
	// shutdown() 및 setsockopt() 함수 호출 없이
	// closesocket()만 호출하면 문제 없이 동작하나
	// 
	// shutdown() 및 setsockopt() 함수 호출 후
	// closesocket()을 호출하면 completion routine이 완료되어
	// blocking중인 accpet() 함수로 실행 흐름이 넘어가는데
	// 이 때 ntdll 액세스 위반이 발생

	int ret = shutdown(closeSocket, SD_BOTH);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] shutdown(): %d",
			WSAGetLastError());

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	ret = setsockopt(
		closeSocket,
		SOL_SOCKET,
		SO_LINGER,
		reinterpret_cast<const char*>(&lingerOption),
		sizeof(lingerOption)
	);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] setsockopt(): %d",
			WSAGetLastError());

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		// socket option 적용에 에러가 발생했어도
		// 소켓은 닫아주자
		closesocket(closeSocket);
		return false;
	}

	closesocket(closeSocket);
	return true;
}

bool OverlappedCallback::BindAndListen(int bindPort)
{
	SOCKADDR_IN serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(bindPort);

	int ret = bind(
		mListenSocket,
		reinterpret_cast<const sockaddr*>(&serverAddr),
		sizeof(serverAddr)
	);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] bind(): %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	ret = listen(mListenSocket, 5);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] listen(): %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

bool OverlappedCallback::StartServer()
{
	bool ret = CreateAccepterThread();
	if (false == ret)
	{
		return false;
	}

	return true;
}

bool OverlappedCallback::CreateAccepterThread()
{
	unsigned int threadID{ 0 };

	mAccepterThread = reinterpret_cast<HANDLE>(
		_beginthreadex(
			NULL,
			0,
			CallAccepterThread,
			this,
			CREATE_SUSPENDED,
			&threadID
		)
		);

	if (NULL == mAccepterThread)
	{
		wsprintf(mErrorMessage,
			L"[에러] _beginthreadex(): %d",
			errno); // thread 생성 실패 이유는 _errno()를 호출한 뒤 역참조한 정수값으로 알 수 있다.

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	mIsAccepterRun = true;

	ResumeThread(mAccepterThread);
	return true;
}

void OverlappedCallback::AccepterThread()
{
	SOCKADDR_IN clientAddr{};
	int addrLen = sizeof(clientAddr);

	while (mIsAccepterRun)
	{
		// WSA_FLAG_OVERLAPPED flag로 listen socket을 생성한 것은
		// overlapped IO를 사용하겠다는 의미지
		// non-blocking 모드를 사용하겠다는 의미가 아님
		// 즉, 명시적으로 FIONBIO를 세팅해주지 않으면,
		// accept()에서 blocking 된다는 뜻임
		// 그런데 accpet()에서 blocking 되면 이 thread를 종료하는 방법이 강제 종료 밖에 없는데?
		// 그래서 InitSocket()에서 non-blocking 모드로 세팅함
		SOCKET clientSocket = accept(
			mListenSocket,
			reinterpret_cast<sockaddr*>(&clientAddr),
			&addrLen
		);

		// 반환값이 WSAEWOULDBLOCK이라면,
		// 아무런 client 접속 요청이 없었다는 의미
		if (WSAEWOULDBLOCK == GetLastError())
		{
			continue;
		}

		if (INVALID_SOCKET == clientSocket)
		{
			// closesocket()이 호출되어 accept()함수가 반환되었고
			// 이 때 반환값이 WSAEINTR이다.
			if (WSAEINTR == GetLastError())
			{
				return;
			}

			wsprintf(mErrorMessage,
				L"[에러] accept(): %d",
				WSAGetLastError());

			MessageBox(mHwnd,
				mErrorMessage,
				NULL,
				MB_OK);

			continue;
		}

		// client와 통신할 전용 socket이 생성되었기 때문에
		// 데이터를 수신할 준비(WSARecv()를 호출하러 가자)
		bool ret = BindRecv(clientSocket);
		if (false == ret)
		{
			return;
		}
	}
}

void CALLBACK CompletionRoutine(DWORD error,
	DWORD transferred,
	LPWSAOVERLAPPED overlapped,
	DWORD flags)
{
	// 인자로 넘어온 Overlapped*를 OverlappedEx*로 캐스팅하면
	// OverlappedEx에서 정의한 다른 멤버들을 사용할 수 있음
	OverlappedEx* pOverlappedEx = reinterpret_cast<OverlappedEx*>(overlapped);

	OverlappedCallback* pOverlappedCallback =
		reinterpret_cast<OverlappedCallback*>(pOverlappedEx->mClassPtr);

	wchar_t errorMessage[128]{};

	// 접속 종료
	if (0 == transferred)
	{
		pOverlappedCallback->CloseSocket(pOverlappedEx->mClientSocket);
	}
	// 중첩 IO에서 에러가 발생했고
	else if (0 != error)
	{
		wsprintf(errorMessage,
			L"[에러] CompletionRoutine(): %d",
			error);

		// message box를 특정 윈도우가 소유할 필요는 없는 듯?
		MessageBox(NULL,
			errorMessage,
			NULL,
			MB_OK);
	}
	else
	{
		switch (pOverlappedEx->mOperation)
		{
		case eOperation::OP_RECV:
		{
			// 받은만큼 echo 하는 것이라 null을 굳이 넣지 않아도 되긴 하다.
			pOverlappedEx->mBuffer[transferred] = '\0';

			// 메시지 수신이 완료되었으니
			// WSASend()를 호출해서
			// 메시지를 송신하도록 OS 요청
			pOverlappedCallback->SendMsg(
				pOverlappedEx->mClientSocket,
				pOverlappedEx->mBuffer,
				transferred
			);
		}
		break;

		case eOperation::OP_SEND:
		{
			// 송신이 완료되었으니
			// client에게 메시지를 수신하기 위해
			// OS에게 요청하러 가자
			pOverlappedCallback->BindRecv(pOverlappedEx->mClientSocket);
		}
		break;

		default:
		{
			wsprintf(errorMessage,
				L"[에러] 정의되지 않은 연산");

			MessageBox(NULL,
				errorMessage,
				NULL,
				MB_OK);
		}
		break;
		}
	}

	// WSASend(), WSARecv() 호출 시 매번 새로 동적 할당하기 때문에
	// IO 작업이 완료되었다면 동적 할당한 메모리를 해제하자 
	delete pOverlappedEx;
}

bool OverlappedCallback::BindRecv(SOCKET socket)
{
	DWORD flag{ 0 };
	DWORD recvNumBytes{ 0 };

	OverlappedEx* pOverlappedEx = new OverlappedEx{};

	// overlapped 구조체를 0으로 밀지 않으면 이상한 에러가 발생한다고 하는데
	// 정확히는 아직 모르겠다.
	ZeroMemory(reinterpret_cast<void*>(&(pOverlappedEx->mWSAOverlapped)),
		sizeof(WSAOVERLAPPED));

	pOverlappedEx->mWSABuffer.len = MAX_SOCKBUF;
	pOverlappedEx->mWSABuffer.buf = pOverlappedEx->mBuffer;
	pOverlappedEx->mClientSocket = socket;
	pOverlappedEx->mOperation = eOperation::OP_RECV;

	// this 포인터를 넣어두면
	// Overlapped Input이 완료되면 this로 SendMsg()를 호출할 수 있고
	// Overlapped Output이 완료되면 this로 BindRecv()를 호출할 수 있다.
	pOverlappedEx->mClassPtr = this;

	int ret = WSARecv(
		socket,
		&(pOverlappedEx->mWSABuffer),
		1,
		&recvNumBytes,
		&flag,
		reinterpret_cast<LPWSAOVERLAPPED>(pOverlappedEx),
		CompletionRoutine
	);

	// 에러가 발생했는데 입출력 보류가 아니면
	// client socket이 끊어진 것으로 판단
	if (SOCKET_ERROR == ret && ERROR_IO_PENDING != WSAGetLastError())
	{
		wsprintf(mErrorMessage,
			L"[에러] WSARecv(): %d",
			WSAGetLastError());

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);
	}

	return true;
}

bool OverlappedCallback::SendMsg(SOCKET socket, char* pMsg, int len)
{
	DWORD recvNumBytes{ 0 };

	OverlappedEx* pOverlappedEx = new OverlappedEx{};

	// ZeroMemory
	memset(reinterpret_cast<void*>(&pOverlappedEx->mWSAOverlapped),
		0x00,
		sizeof(pOverlappedEx->mWSAOverlapped));

	// CopyMemory
	memcpy(reinterpret_cast<void*>(pOverlappedEx->mBuffer),
		reinterpret_cast<const void*>(pMsg),
		len);

	pOverlappedEx->mWSABuffer.buf = pOverlappedEx->mBuffer;
	pOverlappedEx->mWSABuffer.len = len;
	pOverlappedEx->mClientSocket = socket;
	pOverlappedEx->mOperation = eOperation::OP_SEND;
	pOverlappedEx->mClassPtr = this;

	int ret = WSASend(
		socket,
		&(pOverlappedEx->mWSABuffer),
		1,
		&recvNumBytes,
		0,
		reinterpret_cast<LPWSAOVERLAPPED>(&(pOverlappedEx->mWSAOverlapped)),
		CompletionRoutine
	);

	if (SOCKET_ERROR == ret && ERROR_IO_PENDING != WSAGetLastError())
	{
		wsprintf(mErrorMessage,
			L"[에러] WSASend(): %d",
			WSAGetLastError());

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);
	}

	return true;
}