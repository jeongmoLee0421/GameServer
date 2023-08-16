#include <process.h>
#include <WinSock2.h>

#pragma comment(lib, "ws2_32")

#include "IOCompletionPort.h"

enum class eOperation
{
	OP_RECV,
	OP_SEND,
};

// WSASend() 또는 WSARecv() 함수를 호출할 때 인자로 OVERLAPPED 구조체를 넘기고
// GetQueuedCompletionStatus() 함수를 호출할 때 OVERLAPPED 구조체 정보를 가져온다.
// 이 떄 OverlappedEX 구조체로 캐스팅하면 추가적인 더 많은 정보를 얻어올 수 있다.
struct OverlappedEx
{
	WSAOVERLAPPED mWSAOverlapped;
	SOCKET mClientSocket;
	WSABUF mWSABuf;
	char mBuffer[MAX_SOCKBUF];
	eOperation mOperation;
};

struct ClientInfo
{
	ClientInfo()
		: mClientSocket(INVALID_SOCKET)
	{
		memset(&mRecvOverlappedEx, 0x00, sizeof(OverlappedEx));
		memset(&mSendOverlappedEx, 0x00, sizeof(OverlappedEx));
	}

	SOCKET mClientSocket;
	OverlappedEx mRecvOverlappedEx;
	OverlappedEx mSendOverlappedEx;
};

// thread가 실행할 작업 함수로 멤버 함수를 넣을 수 없기 때문에
// (_beginthreadex() 함수는 실행할 작업 함수가 __stdcall 규약을 지켜야 하는데
// 멤버 함수를 사용하려면 this포인터를 기반으로 하는 __thiscall 규약을 따라야 한다.)
// 매개 변수로 객체의 포인터를 넘겨서
// 함수 내부에서 멤버 함수를 호출하자.
unsigned int WINAPI CallWorkerThread(LPVOID p)
{
	IOCompletionPort* pIOCompletionPort = reinterpret_cast<IOCompletionPort*>(p);

	pIOCompletionPort->WorkerThread();

	return 0;
}

unsigned int WINAPI CallAccepterThread(LPVOID p)
{
	IOCompletionPort* pIOCompletionPort = reinterpret_cast<IOCompletionPort*>(p);

	pIOCompletionPort->AccepterThread();

	return 0;
}

IOCompletionPort::IOCompletionPort()
	: mListenSocket{ INVALID_SOCKET }
	, mClientCount{ 0 }
	, mAccepterThread{ NULL }
	, mIOCP{ NULL }
	, mIsWorkerRun{ false }
	, mIsAccepterRun{ false }
	, mErrorMessage{ 0, }
{
	mClientInfo = new ClientInfo[MAX_CLIENT];

	for (int i = 0; i < MAX_WORKERTHREAD; ++i)
	{
		mWorkerThread[i] = NULL;
	}
}

IOCompletionPort::~IOCompletionPort()
{
	WSACleanup();

	DestroyThread();

	if (mClientInfo)
	{
		delete[] mClientInfo;
		mClientInfo = nullptr;
	}
}

bool IOCompletionPort::InitSocket()
{
	WSADATA wsaData{};
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (0 != ret)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] WSAStartup() 함수 실패: %d",
			ret
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	// overlapped IO를 사용하는 listen socket을 생성했으나,
	// 실제로 overlapped IO를 사용하는 socket은 client socket이다.
	// listen socket으로부터 생성된 client socket은
	// 따로 wsa_flag_overlapped를 세팅하지 않았지만,
	// WSASend() 또는 WSARecv() 함수 호출에서 Overlapped 구조체를 같이 넘기면,
	// Overlapped IO를 사용하는 것이고(바로 반환하면서 다음 코드 진행)
	// Overlaped 구조체를 넘기지 않으면
	// Overlapped IO를 사용하지 않는 것이다(해당 코드에서 block되어 완료될때 까지 코드 진행 불가).
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
		wsprintf(
			mErrorMessage,
			L"[에러] WSASocket() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	// 소켓 초기화 성공
	return true;
}

void IOCompletionPort::CloseSocket(ClientInfo* pClientInfo, bool isForce)
{
	struct linger lingerOption { 0, 0 };

	if (isForce)
	{
		lingerOption.l_onoff = 1;
	}

	int ret = shutdown(pClientInfo->mClientSocket, SD_BOTH);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] shutdown() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return;
	}

	// setsockopt()에서 자꾸 10038에러(유효한 소켓이 아님)가 발생했는데
	// 2번에 한 번씩 규칙적으로 발생하길래 무엇이 원인인가 했더니
	// pClientInfo->mClientSocket을 인자로 주지 않고
	// mClientInfo->mClientSocket을 인자로 주었던게 원인이었다.
	// 첫 번째 accept 후에 다음 ClientInfo 위치로 두 번째를 얻어오고
	// 두 번째 accept 후에는 다시 첫번째 ClientInfo 위치를 얻어온다.
	// 두 번째 socket을 close할 때 두 번쨰 socket에 대해서 setsockopt()를 해야하지만
	// mClientInfo의 가장 첫번째 socket인, 이미 닫힌 socket에 적용되어서 문제가 발생했다.
	ret = setsockopt(
		pClientInfo->mClientSocket,
		SOL_SOCKET,
		SO_LINGER,
		reinterpret_cast<const char*>(&lingerOption),
		sizeof(lingerOption)
	);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] setsockopt() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return;
	}

	ret = closesocket(pClientInfo->mClientSocket);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] closesocket() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return;
	}

	// INVALID_SOCKET을 반드시 넣어줘야 한다.
	// ClientInfo 배열에서 비어있는 공간을 찾을 때
	// INVALID_SOCKET인지 아닌지 비교하여 찾기 때문
	pClientInfo->mClientSocket = INVALID_SOCKET;
}

bool IOCompletionPort::BindAndListen(int bindPort)
{
	SOCKADDR_IN serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(bindPort);

	int ret = bind(mListenSocket,
		reinterpret_cast<const sockaddr*>(&serverAddr),
		sizeof(serverAddr));

	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] bind() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	ret = listen(mListenSocket, 5);

	if (SOCKET_ERROR == ret)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] listen() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	// 서버 등록 성공
	return true;
}

bool IOCompletionPort::StartServer()
{
	// IO Completion Port 객체를 생성할 때는
	// 1,2,3번째 인자는 NULL을 넣어주고
	// 마지막 인자에 동시에 실행 가능한 thread의 개수를 넣어주면 된다.
	// 0을 넣게되면 머신의 CPU개수와 동일하게 설정된다.
	mIOCP = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE,
		NULL,
		NULL,
		0
	);

	if (NULL == mIOCP)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] CreateIoCompletionPort() 함수 실패: %d",
			GetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	// worker thread에서
	// GetQueuedCompletionStatus() 함수를 호출할 때
	// IOCP 객체가 생성되어 있지 않으면
	// ERROR_INVALID_HANDLE 에러 발생
	// IOCP 객체를 먼저 생성해놓고 thread를 생성하자
	mIsWorkerRun = true;
	bool ret = CreateWorkerThread();

	if (false == ret)
	{
		return false;
	}

	mIsAccepterRun = true;
	ret = CreateAccepterThread();

	if (false == ret)
	{
		return false;
	}

	// 서버 시작
	return true;
}

bool IOCompletionPort::CreateWorkerThread()
{
	unsigned int threadID{ 0 };

	// Waiting Thread Queue에서 대기하고 있을 thread 생성
	// 권장되는 수는 2 * n + 1
	// 여기서 n은 cpu 개수
	for (int i = 0; i < MAX_WORKERTHREAD; ++i)
	{
		mWorkerThread[i] = reinterpret_cast<HANDLE>(_beginthreadex(
			NULL,
			0,
			CallWorkerThread,
			this,
			CREATE_SUSPENDED,
			&threadID
		));

		if (NULL == mWorkerThread)
		{
			wsprintf(
				mErrorMessage,
				L"[에러] _beginthreadex() 함수 실패: %d",
				errno
			);

			MessageBox(
				NULL,
				mErrorMessage,
				NULL,
				MB_OK
			);

			return false;
		}

		ResumeThread(mWorkerThread[i]);
	}

	// worker thread 시작
	return true;
}

bool IOCompletionPort::CreateAccepterThread()
{
	unsigned int threadID{ 0 };

	mAccepterThread = reinterpret_cast<HANDLE>(_beginthreadex(
		NULL,
		0,
		CallAccepterThread,
		this,
		NULL,
		&threadID
	));

	if (NULL == mAccepterThread)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] _beginthreadex() 함수 실패: %d",
			errno
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	ResumeThread(mAccepterThread);

	// accepter thread 시작
	return true;
}

ClientInfo* IOCompletionPort::GetEmptyClientInfo()
{
	for (int i = 0; i < MAX_CLIENT; ++i)
	{
		// INVALID_SOCKET이면 해당 공간은 접속한 client가 없다는 뜻이고
		if (INVALID_SOCKET == mClientInfo[i].mClientSocket)
		{
			// 그 때 ClientSocket을 멤버로 가지는
			// ClientInfo를 반환
			return &mClientInfo[i];
		}
	}

	return nullptr;
}

bool IOCompletionPort::BindIOCompletionPort(ClientInfo* pClientInfo)
{
	HANDLE hIOCP{};

	// IO Completion Port와 socket을 연결

	// Completion Key 매개변수로
	// ClientInfo 변수가 가리키고 있는 객체의 주소를
	// DWORD64(unsigned long long)으로 재해석 해서 넣었다.
	// 
	// 동적 할당한 주소는 client마다 다르기 때문에 unique하며,
	// 주소가 다르기 때문에 당연히 비트 값도 다를 것이고
	// unsigned long long으로 해석한 그 값도 다를 것이다.
	// 
	// 나중에 GetQueuedCompletionStatus() 함수에서
	// 이 값을 ClientInfo*로 복구시켜서 사용할 것이다.
	hIOCP = CreateIoCompletionPort(
		reinterpret_cast<HANDLE>(pClientInfo->mClientSocket),
		mIOCP,
		reinterpret_cast<DWORD64>(pClientInfo),
		0);

	// 함수가 성공하면
	// 두 번째 인자로 넣은 IOCP 객체의 핸들값을 그대로 반환한다.
	if (NULL == hIOCP || mIOCP != hIOCP)
	{
		wsprintf(
			mErrorMessage,
			L"[에러] CreateIoCompletionPort() 함수 실패: %d",
			GetLastError()
		);


		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	return true;
}

bool IOCompletionPort::BindRecv(ClientInfo* pClientInfo)
{
	DWORD flag{};
	DWORD recvNumBytes{};

	pClientInfo->mRecvOverlappedEx.mWSABuf.len = MAX_SOCKBUF;
	pClientInfo->mRecvOverlappedEx.mWSABuf.buf = pClientInfo->mRecvOverlappedEx.mBuffer;
	pClientInfo->mRecvOverlappedEx.mOperation = eOperation::OP_RECV;

	// OS에 데이터 수신 요청
	int ret = WSARecv(
		pClientInfo->mClientSocket,
		&(pClientInfo->mRecvOverlappedEx.mWSABuf),
		1,
		&recvNumBytes,
		&flag,
		reinterpret_cast<LPWSAOVERLAPPED>(&(pClientInfo->mRecvOverlappedEx)),
		NULL
	);

	if (SOCKET_ERROR == ret && ERROR_IO_PENDING != WSAGetLastError())
	{
		wsprintf(
			mErrorMessage,
			L"[에러] WSARecv() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	return true;
}

bool IOCompletionPort::SendMsg(ClientInfo* pClientInfo, char* pMessage, int length)
{
	DWORD sendNumBytes{ 0 };

	memcpy(pClientInfo->mSendOverlappedEx.mBuffer, pMessage, length);

	pClientInfo->mSendOverlappedEx.mWSABuf.len = length;
	pClientInfo->mSendOverlappedEx.mWSABuf.buf = pClientInfo->mSendOverlappedEx.mBuffer;
	pClientInfo->mSendOverlappedEx.mOperation = eOperation::OP_SEND;

	int ret = WSASend(
		pClientInfo->mClientSocket,
		&(pClientInfo->mSendOverlappedEx.mWSABuf),
		1,
		&sendNumBytes,
		0,
		reinterpret_cast<LPWSAOVERLAPPED>(&(pClientInfo->mSendOverlappedEx.mWSAOverlapped)),
		NULL
	);

	// socket_error라면 client socket이 끊어진 것으로 처리
	if (SOCKET_ERROR == ret && ERROR_IO_PENDING != WSAGetLastError())
	{
		wsprintf(
			mErrorMessage,
			L"[에러] WSASend() 함수 실패: %d",
			WSAGetLastError()
		);

		MessageBox(
			NULL,
			mErrorMessage,
			NULL,
			MB_OK
		);

		return false;
	}

	return true;
}

void IOCompletionPort::WorkerThread()
{
	// GQCS() 함수에서 completion key를 재해석하여
	// 사용할 변수
	ClientInfo* pClientInfo{ nullptr };

	// IO 작업에서 전송된 데이터 크기
	DWORD ioSize{};
	LPOVERLAPPED pOverlapped{ nullptr };

	while (mIsWorkerRun)
	{
		// GetQueuedCompletionStatus() 함수를 호출했을 때
		// 완료된 IO 작업이 없다면,
		// 호출한 thread는 Waiting Thread Queue에 들어가 대기하고 있는다.

		// 완료된 IO 작업이 추가되면,
		// IO Completion Queue에서 완료된 IO 작업에 대한 정보를 하나 꺼내서
		// 후 처리를 진행한다.

		// 서버를 종료할 때
		// 완료된 IO 작업이 없어 GetQueuedCompletionStatus() 함수에서 block되어
		// Waiting Thread Queue에서 대기하고 있는 thread를 안전하게 종료하기 위해
		// PostQueuedCompletionStatus() 함수에 사용자 메시지를 넣어서 호출하면,
		// 이를 처리하기위해 thread가 깨어나고 안전하게 종료한다.
		bool success = GetQueuedCompletionStatus(
			mIOCP,
			&ioSize,
			reinterpret_cast<PDWORD64>(&pClientInfo), // completion key를 재해석해서 받자
			&pOverlapped,
			INFINITE
		);

		if (false == success)
		{
			wsprintf(
				mErrorMessage,
				L"[에러] GetQueuedCompletionStatus() 실패 %d",
				GetLastError()
			);

			/*MessageBox(
				NULL,
				mErrorMessage,
				NULL,
				MB_OK
			);*/

			// false를 반환하더라도
			// 다른 작업들은 계속해서 처리하도록
			continue;
		}

		// PostQueuedCompletionStatus() 함수를 호출할 때
		// 전송 바이트 수를 0으로
		// overlapped 구조체에 대한 값은 nullptr로 세팅하여
		// GetQueuedCompletionStatus() 함수에서
		// 이를 그대로 확인하여 while문을 탈출한다.
		if (true == success && 0 == ioSize && nullptr == pOverlapped)
		{
			mIsWorkerRun = false;
			continue;
		}

		// client가 접속을 끊었을 때인데
		// GQCS() 함수는 true를 반환하면서,
		// 전송 바이트는 0이다.
		// 그리고 명백하게 종료 메시지가 날아온 것이기 때문에
		// overlapped 포인터 변수는 nullptr이 아님.
		if (true == success && 0 == ioSize)
		{
			wsprintf(
				mErrorMessage,
				//L"[에러] socket(%llu) 접속 끊김",
				// 
				// unsgined long long의 서식 지정자는
				// C++ 표준에서는 %llu가 맞는데
				// VS compiler에서 제대로 출력 못해서
				// Vs compiler에서 제공하는 %I64u를 사용함
				L"[에러] socket(%I64u) 접속 끊김",
				pClientInfo->mClientSocket
			);

			/*MessageBox(
				NULL,
				mErrorMessage,
				NULL,
				MB_OK
			);*/

			CloseSocket(pClientInfo);
			--mClientCount;

			continue;
		}

		// IOCP에서 완료된 Overlapped IO 작업 결과를 가져오지 못해서
		// 함수 호출에 실패하는 경우
		// lpOverlapped 인자값이 nullptr이 될 수 있어서 확인
		if (nullptr == pOverlapped)
		{
			continue;
		}

		OverlappedEx* pOverlappedEx = reinterpret_cast<OverlappedEx*>(pOverlapped);

		// 수신 작업이 완료되면
		// 메시지를 echo하기 위해 송신 요청하자
		if (eOperation::OP_RECV == pOverlappedEx->mOperation)
		{
			SendMsg(pClientInfo,
				pOverlappedEx->mBuffer,
				ioSize);
		}
		// 송신 작업이 완료되었으니
		// 다시 client로부터 메시지를 받을 준비
		else if (eOperation::OP_SEND == pOverlappedEx->mOperation)
		{
			BindRecv(pClientInfo);
		}
		else
		{
			wsprintf(
				mErrorMessage,
				L"socket(%llu) 예외 상황 발생",
				pClientInfo->mClientSocket
			);

			MessageBox(
				NULL,
				mErrorMessage,
				NULL,
				MB_OK
			);
		}
	}
}

void IOCompletionPort::AccepterThread()
{
	SOCKADDR_IN clientAddr{};
	int addrSize = sizeof(clientAddr);

	while (mIsAccepterRun)
	{
		ClientInfo* pClientInfo = GetEmptyClientInfo();
		if (nullptr == pClientInfo)
		{
			wsprintf(
				mErrorMessage,
				L"[에러] client가 가득 찼습니다."
			);

			MessageBox(
				NULL,
				mErrorMessage,
				NULL,
				MB_OK
			);

			return;
		}

		// listen socket을 대상으로 closesocket()을 호출하여
		// blocking 상태를 해제하고 다음 코드로 진행
		pClientInfo->mClientSocket = accept(
			mListenSocket,
			reinterpret_cast<sockaddr*>(&clientAddr),
			&addrSize
		);

		if (INVALID_SOCKET == pClientInfo->mClientSocket)
		{
			// 프로그램을 종료하는 경우
			// accept() 함수에서 blocking 되어 있던
			// listen socket이 INVALID_SOCKET을 반환하며,
			// WSAEINTR를 에러코드로 가진다.
			if (WSAEINTR == WSAGetLastError())
			{
				mIsAccepterRun = false;
			}

			continue;
		}

		// 새로운 client가 접속했으니
		// IOCP 객체와 연결하여
		// 해당 client socket의 Overlapped IO 작업 완료 정보를
		// IO Completion Queue에 넣도록 하자
		bool ret = BindIOCompletionPort(pClientInfo);
		if (false == ret)
		{
			return;
		}

		// 새로 접속한 client가 메시지를 보내는 것을 수신하기 위해
		// WSARecv()를 호출하러 가자
		ret = BindRecv(pClientInfo);
		if (false == ret)
		{
			return;
		}

		// client 접속
		++mClientCount;
	}
}

void IOCompletionPort::DestroyThread()
{
	for (int i = 0; i < MAX_WORKERTHREAD; ++i)
	{
		// Waiting Thread Queue에서 대기 중인 thread가 있을텐데
		// IOCP queue에 작업을 추가하여
		// thread들이 작업을 처리하려고 깨어나면
		// mIsWorkerRun 변수를 false로 처리하여
		// while문을 탈출해서 안전하게 thread를 종료
		PostQueuedCompletionStatus(
			mIOCP,
			0,
			0,
			nullptr
		);
	}

	for (int i = 0; i < MAX_WORKERTHREAD; ++i)
	{
		WaitForSingleObject(mWorkerThread[i], INFINITE);
		CloseHandle(mWorkerThread[i]);
	}

	mIsAccepterRun = false;
	closesocket(mListenSocket);
	WaitForSingleObject(mAccepterThread, INFINITE);
}