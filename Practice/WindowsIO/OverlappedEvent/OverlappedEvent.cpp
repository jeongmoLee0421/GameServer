#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>
#include <process.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32")

#include "OverlappedEvent.h"

// thread 생성할 때 직접 멤버 함수를 넣을 수 없음
// 그래서 멤버 함수 호출을 위한 class를 매개변수로 넣은 전역함수 생성
// 내부에서 캐스팅해서 호출
unsigned int WINAPI CallWorkerThread(LPVOID p)
{
	OverlappedEvent* pOverlappedEvent = reinterpret_cast<OverlappedEvent*>(p);
	pOverlappedEvent->WorkerThread();
	return 0;
}

unsigned int WINAPI CallAccepterThread(LPVOID p)
{
	OverlappedEvent* pOverlappedEvent = reinterpret_cast<OverlappedEvent*>(p);
	pOverlappedEvent->AccepterThread();
	return 0;
}

OverlappedEvent::OverlappedEvent()
	: mClientCount{ 0 }
	, mWorkerThread{ NULL }
	, mAccepterThread{ NULL }
	, mIsWorkerRun{ false }
	, mIsAccepterRun{ false }
	, mErrorMessage{ 0, }
{
	for (int i = 0; i < WSA_MAXIMUM_WAIT_EVENTS; ++i)
	{
		mClientInfo.mClientSocket[i] = INVALID_SOCKET;

		// 초기화 과정에서 64개 전부 만들고 시작
		mClientInfo.mEventHandle[i] = WSACreateEvent();

		// WSAOVERLAPPED 구조체는 0으로 초기화
		// OS 내부에서 사용하는 Internal ~ OffsetHigh 까지 4개의 변수가
		// 0이 아니면 예상치 못한 일이 발생할 수 있다고 한다.
		ZeroMemory(&mClientInfo.mOverlappedEx, sizeof(WSAOVERLAPPED));
	}
}

OverlappedEvent::~OverlappedEvent()
{
	WSACleanup();

	for (int i = 0; i < WSA_MAXIMUM_WAIT_EVENTS; ++i)
	{
		WSACloseEvent(mClientInfo.mEventHandle[i]);
	}

	DestroyThread();
}

bool OverlappedEvent::InitSocket(HWND hwnd)
{
	mHwnd = hwnd;

	WSADATA wsaData{};
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (0 != ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] WSAStartup() 함수 실패: %d",
			ret);

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

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
		wsprintf(mErrorMessage,
			L"[에러] WSASocket() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// 코어를 계속해서 사용할 것임을 알면서도
	// server socket을 non-blocking 모드로 변환하는 이유는
	// 
	// 만약 accpet()에서 block되어 멈춰있다면,
	// accpet thread를 종료하기 위해서는 thread 강제종료 밖에 답이 없다.
	//
	// 하지만 accept()에서 block되지 않고 반환된다면,
	// while문의 조건을 계속해서 검사할 수 있고
	// 조건을 false로 변경해주면 thread를 안전하게 종료할 수 있기 때문이다.
	u_long blockingMode{ 0 };
	ret = ioctlsocket(mClientInfo.mClientSocket[0],
		FIONBIO,
		&blockingMode);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] ioctlsocket() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

void OverlappedEvent::CloseSocket(SOCKET closeSocket, bool isForce)
{
	struct linger lingerOption { 0, 0 };

	// closesocket()에서 non-blocking 되면서 강제 종료
	// 소켓 송신 버퍼에 데이터가 남아있어도 운영체제는 이를 보내지 못하고
	// 상대 소켓이 데이터를 보내도 운영체제는 이를 소켓 수신 버퍼에 받지도 못한다.
	if (true == isForce)
	{
		lingerOption.l_onoff = 1;
	}

	shutdown(closeSocket, SD_BOTH);

	setsockopt(closeSocket,
		SOL_SOCKET,
		SO_LINGER,
		reinterpret_cast<const char*>(&lingerOption),
		sizeof(lingerOption));

	closesocket(closeSocket);

	// 값 복사로 받으면 지역변수인 복사본에 INVALID_SOCKET이 세팅되기 때문에
	// 참조로 받아서 원본에 INVALID_SOCKET 세팅하거나
	// 아니면 CloseSocket() 호출 후에 직접 INVALID_SOCKET 세팅하면 된다.
	// 추후에 GetEmptyIndex()에서 INVALID_SOCKET 여부로 ClientInfo 구조체에서 미사용 공간을 찾기 때문
	closeSocket = INVALID_SOCKET;
}

bool OverlappedEvent::BindAndListen(int bindPort)
{
	SOCKADDR_IN serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(bindPort);

	int ret = bind(
		mClientInfo.mClientSocket[0],
		reinterpret_cast<const sockaddr*>(&serverAddr),
		sizeof(serverAddr)
	);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] bind() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	ret = listen(mClientInfo.mClientSocket[0], 5);
	if (SOCKET_ERROR == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] listen() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	//std::cout << "서버 등록 성공" << std::endl;
	return true;
}

bool OverlappedEvent::StartServer()
{
	mClientInfo.mEventHandle[0] = WSACreateEvent();

	mIsWorkerRun = true;
	bool ret = CreateWorkerThread();
	if (false == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] CreateWorkerThread() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	mIsAccepterRun = true;
	ret = CreateAccepterThread();
	if (false == ret)
	{
		wsprintf(mErrorMessage,
			L"[에러] CreateAccepterThread() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	//std::cout << "서버 시작" << std::endl;
	return true;
}

bool OverlappedEvent::CreateWorkerThread()
{
	unsigned int threadID{ 0 };

	mWorkerThread = reinterpret_cast<HANDLE>(_beginthreadex(
		nullptr,
		0,
		CallWorkerThread, // OverlappedEvent class 를 매개변수로 받는 전역함수
		this, // thread 내부에서 멤버 함수를 호출하기 위함
		CREATE_SUSPENDED,
		&threadID)
		);
	if (NULL == mWorkerThread)
	{
		wsprintf(mErrorMessage,
			L"[에러] mWorkerThread 생성 실패");

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	ResumeThread(mWorkerThread);
	//std::cout << "Worker Thread 시작" << std::endl;

	return true;
}

bool OverlappedEvent::CreateAccepterThread()
{
	unsigned int threadID{ 0 };

	mAccepterThread = reinterpret_cast<HANDLE>(_beginthreadex(
		nullptr,
		0,
		CallAccepterThread, // OverlappedEvent class를 매개변수로 받는 전역함수
		this, // thread 내부에서 멤버 함수를 호출하기 위함
		CREATE_SUSPENDED,
		&threadID)
		);
	if (NULL == mAccepterThread)
	{
		wsprintf(mErrorMessage,
			L"[에러] mAccepterThread 생성 실패");

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	ResumeThread(mAccepterThread);
	//std::cout << "Accepter Thread 시작" << std::endl;

	return true;
}

int OverlappedEvent::GetEmptyIndex()
{
	// 0번 인덱스는 client 접속 요청을 처리하는 server
	for (int i = 1; i < WSA_MAXIMUM_WAIT_EVENTS; ++i)
	{
		if (INVALID_SOCKET == mClientInfo.mClientSocket[i])
		{
			return i;
		}
	}

	return -1;
}

bool OverlappedEvent::BindRecv(int index)
{
	DWORD flag{};
	DWORD recvNumBytes{};

	// client가 새로 접속했으니 event 객체를 새로 생성
	//mClientInfo.mEventHandle[index] = WSACreateEvent();
	// 초기화 과정에서 event 객체 max 만들고 시작

	// Overlapped IO를 하기 위한 정보 세팅

	// send(), recv()가 완료되면 signaled 상태로 변경될 event 객체 핸들
	mClientInfo.mOverlappedEx[index].mWSAOverlapped.hEvent = mClientInfo.mEventHandle[index];
	mClientInfo.mOverlappedEx[index].mWSABuf.len = static_cast<ULONG>(MAX_SOCKBUF);

	// 송수신에 사용될 버퍼 시작 주소
	mClientInfo.mOverlappedEx[index].mWSABuf.buf = mClientInfo.mOverlappedEx[index].mBuffer;
	mClientInfo.mOverlappedEx[index].mIndex = index;
	mClientInfo.mOverlappedEx[index].mOperation = eOperation::OP_RECV;

	// 커널에게 이 소켓으로 들어오는 데이터를 수신하라고 '요청'하는 것임
	// 추후에 수신이 완료되면, overlapped 구조체에 연동된 handle을 signaled 상태로 변경하고
	// WSAWaitForMultipleEvents() 함수가 반환되면 작업 완료에 대한 처리를 하면 된다.
	// WSARecv()의 후처리는 받은 메시지를 그대로 클라이언트에게 전송하는 것이다.
	int ret = WSARecv(
		mClientInfo.mClientSocket[index],
		&(mClientInfo.mOverlappedEx[index].mWSABuf),
		1,
		&recvNumBytes,
		&flag,
		reinterpret_cast<LPWSAOVERLAPPED>(&mClientInfo.mOverlappedEx[index]),
		NULL
	);

	// socket_error가 발생했더라도
	// error code가 error_io_pending이라면
	// overlapped IO가 올바르게 요청된 것임.
	// 지금 recv가 완료되지 않은 것이지 추후에 완료되는 것
	if (SOCKET_ERROR == ret && ERROR_IO_PENDING != WSAGetLastError())
	{
		wsprintf(mErrorMessage,
			L"[에러] WSARecv() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

bool OverlappedEvent::SendMsg(int index, char* pMsg, int length)
{
	DWORD sendNumBytes{ 0 };

	// 전송할 메시지 복사
	CopyMemory(mClientInfo.mOverlappedEx[index].mBuffer, pMsg, length);

	mClientInfo.mOverlappedEx[index].mWSAOverlapped.hEvent = mClientInfo.mEventHandle[index];
	mClientInfo.mOverlappedEx[index].mWSABuf.len = length;
	mClientInfo.mOverlappedEx[index].mWSABuf.buf = mClientInfo.mOverlappedEx[index].mBuffer;

	// index를 저장해두어야
	// overlapped IO 작업이 완료되면
	// 어떤 socket이 작업 완료된건지 구분 가능
	mClientInfo.mOverlappedEx[index].mIndex = index;
	mClientInfo.mOverlappedEx[index].mOperation = eOperation::OP_SEND;

	int ret = WSASend(
		mClientInfo.mClientSocket[index],
		&(mClientInfo.mOverlappedEx[index].mWSABuf),
		1,
		&sendNumBytes,
		0,
		reinterpret_cast<LPWSAOVERLAPPED>(&mClientInfo.mOverlappedEx[index]),
		NULL);

	if (SOCKET_ERROR == ret && ERROR_IO_PENDING != WSAGetLastError())
	{
		wsprintf(mErrorMessage,
			L"[에러] WSASend() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	return true;
}

void OverlappedEvent::WorkerThread()
{
	while (mIsWorkerRun)
	{
		// Overlapped IO 작업이 완료되면
		// 해당 OVERLAPPED 구조체에 연동된 event 객체가 signaled 상태로 변경되고
		// WSAWaitForMultipleEvents() 함수가 반환
		DWORD objIndex = WSAWaitForMultipleEvents(
			WSA_MAXIMUM_WAIT_EVENTS, // 지정한 개수만큼 event 핸들이 모두 유효해야 함. 아니면 WSA_INVALID_HANDLE(6) 에러
			mClientInfo.mEventHandle,
			false,
			INFINITE,
			false
		);

		if (WSA_WAIT_FAILED == objIndex)
		{
			wsprintf(mErrorMessage,
				L"[에러] WSAWaitForMultipleEvents() 함수 실패: %d",
				WSAGetLastError());

			MessageBox(mHwnd,
				mErrorMessage,
				NULL,
				MB_OK);

			break;
		}

		// event 객체의 상태를 non-signaled 상태로 변경해야
		// 다음 WaitForMultipleEvents() 함수에서 기다릴 수 있음
		WSAResetEvent(mClientInfo.mEventHandle[objIndex]);

		// client 접속 요청을 server가 수락한 경우
		if (WSA_WAIT_EVENT_0 == objIndex)
		{
			continue;
		}

		// overlapped IO 작업이 완료되었으니
		// objIndex에 해당하는 socket의 작업을 처리하자
		OverlappedResult(objIndex);
	}
}

void OverlappedEvent::AccepterThread()
{
	SOCKADDR_IN clientAddr{};
	int addrLen = sizeof(clientAddr);

	while (mIsAccepterRun)
	{
		int index = GetEmptyIndex();
		if (-1 == index)
		{
			wsprintf(mErrorMessage,
				L"[에러] client가 가득 찼습니다.");

			MessageBox(mHwnd,
				mErrorMessage,
				NULL,
				MB_OK);
			break;
		}

		// 만약 server socket이 blocking 모드라면,
		// accept() 함수에서 block되어서
		// thread를 종료하는 방법이 강제 종료 밖에 없다.
		// 하지만 non-blocking 모드라면,
		// 코어를 할당 받았을 때 계속해서 while문을 수행할 것이고
		// mIsAccepterRun 변수를 false로 바꿔주면 while문을 탈출해서
		// 안전하게 thread를 종료할 수 있다.
		mClientInfo.mClientSocket[index] = accept(
			mClientInfo.mClientSocket[0],
			reinterpret_cast<sockaddr*>(&clientAddr),
			&addrLen
		);
		if (INVALID_SOCKET == mClientInfo.mClientSocket[index])
		{
			wsprintf(mErrorMessage,
				L"[에러] accept() 함수 실패: %d",
				WSAGetLastError());

			MessageBox(mHwnd,
				mErrorMessage,
				NULL,
				MB_OK);

			break;
		}

		// 전용 client socket을 생성했으니
		// 메시지를 받을 준비를 해야한다.
		bool ret = BindRecv(index);
		if (false == ret)
		{
			wsprintf(mErrorMessage,
				L"[에러] BindRecv() 함수 실패");

			MessageBox(mHwnd,
				mErrorMessage,
				NULL,
				MB_OK);

			break;
		}

		/*char addrBuf[16]{};
		std::cout << "client 접속: IP(" << inet_ntop(AF_INET, &clientAddr.sin_addr, addrBuf, sizeof(addrBuf)) <<
			") SOCKET(" << mClientInfo.mClientSocket[index] << ")" << std::endl;*/

		++mClientCount;

		// 새로운 client가 접속했기 때문에
		// WaitForMultipleEvents() 함수를 다시 호출해서
		// 접속 받은 유저의 이벤트 객체를 등록시키기 위함
		// 
		// WaitForMultipleEvents() 함수가
		// event 핸들 배열의 복사본을 가지고 검사를 하기 때문에
		// 다시 등록해야 갱신된 event 핸들 배열 복사본으로 검사를 하기 위함이 아닐까라는 생각
		WSASetEvent(mClientInfo.mEventHandle[0]);
	}
}

void OverlappedEvent::OverlappedResult(int index)
{
	DWORD transfer{ 0 };
	DWORD flags{ 0 };

	bool ret = WSAGetOverlappedResult(
		mClientInfo.mClientSocket[index],
		&(mClientInfo.mOverlappedEx[index].mWSAOverlapped),
		&transfer,
		false, // false를 준 이유는 overlapped IO가 완료(명백하게 작업이 끝남)되어서 WaitForMultipleEvents()
		&flags // 함수가 반환되어 여기로 넘어왔기 때문에 기다릴 이유가 없다.
	);

	// WaitForMultipleEvents()에서 반환되어 왔기 때문에
	// IO 작업은 완료되었기 떄문에
	// 이 에러를 제외한 나머지 에러를 잡음
	if (false == ret && WSA_IO_INCOMPLETE != WSAGetLastError())
	{
		wsprintf(mErrorMessage,
			L"[에러] WSAGetOverlappedResult() 함수 실패: %d",
			WSAGetLastError());

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		return;
	}

	// 접속 끊김
	if (0 == transfer)
	{
		wsprintf(mErrorMessage,
			L"[접속 끊김] socket: %I64d",
			mClientInfo.mClientSocket[index]);

		MessageBox(mHwnd,
			mErrorMessage,
			NULL,
			MB_OK);

		CloseSocket(mClientInfo.mClientSocket[index]);

		// empty index를 찾기 위함
		mClientInfo.mClientSocket[index] = INVALID_SOCKET;

		// 새로운 이벤트 핸들로 덮어씌워지기 전에 event 객체 제거해달라고 OS에 요청
		//WSACloseEvent(mClientInfo.mEventHandle[index]);
		// 소멸 과정에서 event 객체 max개 한번에 닫을 거임

		--mClientCount;

		return;
	}

	OverlappedEx* pOverlappedEx = &mClientInfo.mOverlappedEx[index];
	switch (pOverlappedEx->mOperation)
	{
	case eOperation::OP_RECV:
	{
		pOverlappedEx->mBuffer[transfer] = '\0';
		//std::cout << "[수신] bytes: " << transfer << " msg: " << pOverlappedEx->mBuffer << std::endl;

		// 수신한 메시지를 echo
		SendMsg(index, pOverlappedEx->mBuffer, transfer);
	}
	break;

	case eOperation::OP_SEND:
	{
		pOverlappedEx->mBuffer[transfer] = '\0';
		//std::cout << "[송신] bytes: " << transfer << " msg: " << pOverlappedEx->mBuffer << std::endl;

		// 수신을 완료했으니
		// client로부터 다시 메시지를 받을 준비
		BindRecv(index);
	}
	break;

	default:
	{
		//std::cout << "정의되지 않은 operation" << std::endl;
	}
	break;
	}
}

void OverlappedEvent::DestroyThread()
{
	closesocket(mClientInfo.mClientSocket[0]);

	// WaitForMultipleEvents() 함수가 반환되고
	WSASetEvent(mClientInfo.mEventHandle[0]);

	// while의 조건문이 false가 되어 thread 종료
	mIsWorkerRun = false;

	// server socket을 non-blocking 모드로 변경했기 때문에
	// accpet()에서 block 되지 않고 while의 조건문 검사에서
	// false가 되어 thread 종료
	mIsAccepterRun = false;

	WaitForSingleObject(mWorkerThread, INFINITE);
	WaitForSingleObject(mAccepterThread, INFINITE);
}
