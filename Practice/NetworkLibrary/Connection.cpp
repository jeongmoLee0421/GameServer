// AcceptEx() 함수를 호출하려면 mswsock.h가 필요한데
// ws2tcpip.h를 포함하지 않으면, AcceptEx() 함수를 식별하지 못함.
// 정확한 이유는 찾지 못함
#include <ws2tcpip.h>
#include <mswsock.h>

#include "Log.h"
#include "IOCPServer.h"
#include "Connection.h"

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "Mswsock")

constexpr int PACKET_SIZE_LENGTH = 4;

Connection::Connection()
	: mListenSocket{ INVALID_SOCKET }
	, mClientSocket{ INVALID_SOCKET }
	, mRecvOverlappedEx{ nullptr }
	, mSendOverlappedEx{ nullptr }
	, mSendBufSize{ 0 }
	, mRecvBufSize{ 0 }
	, mAddressBuf{ 0, }
	, mIsClosed{ false }
	, mIsConnected{ false }
	, mIsSending{ true }
	, mClientIP{ 0, }
	, mIndex{ -1 }
	, mIOCP{ NULL }
	, mSendIORefCount{ 0 }
	, mRecvIORefCount{ 0 }
	, mAcceptIORefCount{ 0 }
{
}

Connection::~Connection()
{
}

void Connection::InitializeConnection()
{
	// 기존 client가 접속 종료하면, CloseConnection()이 호출되고
	// 새로운 client를 받기 위해 변수 초기화

	ZeroMemory(mClientIP, sizeof(mClientIP));
	mClientSocket = INVALID_SOCKET;

	mIsConnected = false;
	mIsClosed = false;

	// 초기값을 true로 세팅해야
	// 첫 SendPost() 함수 호출에서 WSASend() 호출 코드로 진입 가능
	mIsSending = true;

	mSendIORefCount = 0;
	mRecvIORefCount = 0;
	mAcceptIORefCount = 0;

	mSendRingBuffer.Initialize();
	mRecvRingBuffer.Initialize();
}

bool Connection::CreateConnection(InitConfig& initConfig)
{
	// Connection Manager에서 Connection Pool을 제작할 때
	// 한 번에 여러 Connection 객체를 생성하는데
	// Connection 객체마다 생성 직후 단 한 번만 불리는 함수.

	mIndex = initConfig.mIndex;
	mListenSocket = initConfig.mListenSocket;

	mRecvOverlappedEx = new OVERLAPPED_EX{ this };
	mSendOverlappedEx = new OVERLAPPED_EX{ this };

	mRecvBufSize = initConfig.mRecvBufSize;
	mSendBufSize = initConfig.mSendBufSize;

	mRecvRingBuffer.Create(mRecvBufSize * initConfig.mRecvBufCnt);
	mSendRingBuffer.Create(mSendBufSize * initConfig.mSendBufCnt);

	// connection 객체를 생성했으면,
	// cilent의 접속 요청 받을 준비
	return BindAcceptExSock();
}

bool Connection::CloseConnection(bool isForce)
{
	// client와 접속이 끊기면,
	// CloseConnection() 함수가 호출되어 소켓과의 연결을 종료하고
	// InitializeConnection()을 호출해서 변수를 초기화를 하고
	// BindAcceptExSock()을 호출해서 새로운 client를 받을 준비

	// SendPost()에서 모든 데이터를 전송하고 나서 작업 완료 통지에서 다시 SendPost()를 호출
	// (1개의 스레드만 1개의 Connection 객체에 대해 SendPost() 호출)
	// RecvPost()에서 데이터 수신이 완료되면 작업 완료 통지에서 다시 RecvPost() 호출
	// (1개의 스레드만 1개의 Connection 객체에 대해 RecvPost() 호출)
	// 그래서 하나의 Connection 객체에 대해서 오직 하나의 스레드만 접근하는 것으로 확인했는데
	// 왜 lock이 필요한 것인가..?
	Monitor::Owner lock{ mConnectionSyncObj };

	// 기본은 우아한 종료 
	struct linger lingerOption { 0, 0 };

	// linger값을 활성화(l_onoff) 시키고
	// timeout값(l_linger)이 0이면,
	// 강제 종료(전송할 데이터가 OS 전송 버퍼에 있더라도 보내지 않고 종료)
	if (isForce)
	{
		lingerOption.l_onoff = 1;
	}

	// Connection 객체를 Connection Manager가 관리하고 있는데
	// 이 Connection객체의 socket을 연결 종료하기로 했기 떄문에
	// IOCPServer의 OnClose() 함수를 호출해서
	// Connection 객체를 담고 있던 컨테이너에서 찾아서 삭제한다.
	if (nullptr != IOCPServer::GetIOCPServer() && true == mIsConnected)
	{
		// IOCPServer를 상속한 class가 Chat Server면 Chat Server의 OnClose()가 호출되고
		// 상속한 class가 Game Server면 Game Server의 OnClose()가 호출되고
		// 상속한 class가 Npc Server면 Npc Server의 OnClose()가 호출된다.
		// static 변수이다보니 변수를 공유하기 떄문에 각 서버들은 각자의 프로젝트에서 별개로 생성되어야 한다.
		IOCPServer::GetIOCPServer()->OnClose(this);
	}

	shutdown(mClientSocket, SD_BOTH);
	setsockopt(mClientSocket,
		SOL_SOCKET,
		SO_LINGER,
		reinterpret_cast<const char*>(&lingerOption),
		sizeof(lingerOption));

	closesocket(mClientSocket);
	mClientSocket = INVALID_SOCKET;

	if (mRecvOverlappedEx)
	{
		// 패킷을 수신하는 과정에서 TCP 특성 상 여러번에 걸쳐서 수신하는 경우도 있는데
		// 이 떄 특정 패킷은 완전히 다 수신하지 못하고 잘려서 일부만 받는 경우도 있다.
		// 완전히 받지 못한 패킷에 대해서 몇 바이트까지 받았는지 알고 있어야
		// recv ring buffer의 어디서부터 이어서 패킷을 수신해야하는지 알 수 있다.
		mRecvOverlappedEx->mProcessedBytes = 0;
		mRecvOverlappedEx->mTotalBytes = 0;
	}

	if (mSendOverlappedEx)
	{
		// WSAsend()가 모든 데이터를 전부 송신한다고 보장할 수 없기 때문에
		// 데이터의 일부만 송신되었다면,
		// 총 송신해야 하는 바이트 수 TotalBytes에서
		// 송신에 성공한 바이트 수 mProcessedBytes를 뺀 나머지에 대해 다시 송신 요청
		mSendOverlappedEx->mProcessedBytes = 0;
		mSendOverlappedEx->mTotalBytes = 0;
	}

	InitializeConnection();
	return BindAcceptExSock();
}

bool Connection::BindIOCP(HANDLE hIOCP)
{
	HANDLE retIOCP{ NULL };

	// 새로운 client가 접속했고
	// IOCP 객체에 새로운 client socket을 등록하는 과정인데
	// 나는 굳이 lock을 걸 필요는 없다고 생각한다.
	// 이 함수에서는 CreateIoCompletionPort()만 호출하는데
	// 이 IOCP 객체는 커널이 관리하는 객체고
	// 당연히 커널 내부적으로 동기화를 시키고 있을 것이기 때문이다.
	Monitor::Owner lock{ mConnectionSyncObj };

	// 여러 client가 동시에 접속해서
	// BindIOCP()함수가 병렬적으로 호출된다고 하더라도
	// CreateIoCompletionPort() 함수 내에서 IOCP 객체에 접근할 때
	// kernel이 내부적으로 동기화를 분명 할텐데
	// 사용자 코드에서 lock을 걸지 않아도 된다고 생각
	retIOCP = CreateIoCompletionPort(
		reinterpret_cast<HANDLE>(mClientSocket),
		hIOCP,
		// Connection 객체의 주소를 캐스팅해서 key값으로 넣어줌.
		// Connection 객체들은 배열로 선언되어 메모리에 할당되어 있기 때문에
		// 주소가 겹칠 수 없고
		// 고유한 key값이 될 자격이 있다.
		// 후에 Overlapped IO작업이 끝나고
		// GQCS() 함수에서 완료 통지를 꺼내올 때
		// 이 key값을 Connection class로 받아서
		// Connection 객체의 주소를 복구할 수 있다.
		reinterpret_cast<DWORD64>(this),
		// 그런데 이 방법 말고도
		// GQCS() 함수를 호출해서 IOCP queue에서 작업 완료 통지를 꺼내올 때
		// Overlapped IO를 요청할 때 넘긴 Overlapped 구조체의 포인터를 받아올 수 있는데
		// 이 Overlapped 구조체를 OVERLAPPED_EX로 캐스팅해서 Connection 객체의 주소를 복구할 수도 있다.
		0);

	// 어떤 핸들(여기서는 소켓)을 IOCP 객체에 등록하는
	// CreateIoCompletionPort() 함수가 성공하면,
	// 반환 값은 2번째 인자로 넣어준 IOCP 핸들이다.
	if (NULL != retIOCP || hIOCP != retIOCP)
	{
		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Connection::BindIOCP() | CreateIoCompletionPort() failed: %lu",
			GetLastError());

		return false;
	}

	mIOCP = hIOCP;

	return true;
}

bool Connection::RecvPost(char* pPacketStart, DWORD processedBytes)
{
	if (false == mIsConnected || nullptr == mRecvOverlappedEx)
	{
		return false;
	}

	mRecvOverlappedEx->mOperation = eOperationType::OP_RECV;
	mRecvOverlappedEx->mProcessedBytes = processedBytes;

	// 내가 패킷을 1024바이트 수신했다고 가정해보자.
	// 그 중 1020바이트는 처리가 되었고
	// 1020바이트를 시작으로 6바이트를 받아야 온전한 하나의 패킷을 받은 것인데, 일부인 4바이트만 받은 상태이다.
	// 첫 수신 단계라 가정하고 CurrentMark가 Begin에 위치한다면,
	// mark가 이동할 거리는 4 - (0 - 1020) = 1024가 나온다.
	int movementDistance{ static_cast<int>(processedBytes) -
		static_cast<int>(mRecvRingBuffer.GetCurrentMark() - pPacketStart) };
	// 결국 무슨 뜻이냐면,
	// pPacketStart위치로 이동하고 processedBytes만큼 이동해서 데이터를 받아라

	// RecvRingBuffer의 MoveMark()를 호출해서
	// CurrentMark를 현 위치에서 1024바이트 이동시키고
	// 그 곳에서부터 계속해서 데이터를 수신하면 된다.
	// 왜냐하면, 1024바이트까지는 완전히 수신했으니까
	// 그 다음 메모리가 빈 공간이기 때문이다.
	// 이후에 수십/수백/수천 바이트를 수신하면,
	// 온전히 받은 패킷은 처리하면 되고
	// 잘린 패킷의 정보(잘린 패킷의 시작 위치, 잘린 패킷을 얼마나 받았는지)를 기반으로
	// 수신할 위치를 찾아서 계속해서 수신하면 된다.
	mRecvOverlappedEx->mWSABuf.buf =
		mRecvRingBuffer.MoveMark(movementDistance, mRecvBufSize, processedBytes);

	if (nullptr == mRecvOverlappedEx->mWSABuf.buf)
	{
		// ProcessThread에서 사용하고 있는 ProcessIOCP queue에
		// OP_CLOSE 패킷을 넣어준다.
		// ProcessThread는 위의 완료 통지를 꺼내서
		// Connection class의 CloseConnection() 호출하고
		// CloseConnection() 내부에서 IOCPServer::OnClose()가 호출되면
		// IOCPServer를 상속한 class에서 재정의한 OnClose() 호출
		IOCPServer::GetIOCPServer()->CloseConnection(this);

		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Connection::RecvPost() | Socket[%llu] RecvRingBuffer overflow",
			mClientSocket);

		return false;
	}

	// mWSABuf.buf에는 내가 데이터를 수신할 메모리 위치가 저장되어 있다.
	// 그 위치에서 온전히 받지 못하고 잘린 패킷의 지금까지 받은 바이트 수를 뺴주면,
	// 해당 패킷의 시작 위치를 알 수 있다.
	// 추후에 수신이 완료되어 온전한 하나의 패킷이 완성되면,
	// 이 위치를 시작으로 패킷을 해석한다.
	mRecvOverlappedEx->mPacketStart = mRecvOverlappedEx->mWSABuf.buf - processedBytes;

	memset(&mRecvOverlappedEx->mOverlapped, 0x00, sizeof(mRecvOverlappedEx->mOverlapped));
	IncrementRecvIORefCount();

	DWORD numOfBytesRecvd{ 0 };
	DWORD flag{ 0 };
	int ret = WSARecv(
		mClientSocket,
		&mRecvOverlappedEx->mWSABuf,
		1,
		&numOfBytesRecvd,
		&flag,
		&mRecvOverlappedEx->mOverlapped,
		NULL);

	if (SOCKET_ERROR == ret && WSA_IO_PENDING != WSAGetLastError())
	{
		DecrementRecvIORefCount();

		IOCPServer::GetIOCPServer()->CloseConnection(this);

		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Connection::RecvPost() | WSARecv() failed: %d",
			WSAGetLastError());

		return false;
	}

	return true;
}

bool Connection::SendPost()
{
	// Interlocked 계열 함수는 어떤 작업을 원자적으로 실행하는 함수다.
	// 하나의 작업은 여러개의 명령어로 구성되는데
	// 명령어의 일부만 실행되어 값이 완전히 수정되고 저장되지 않았음에도
	// 다른 thread에게 core를 양보해야 한다면, 그리고 그 core를 할당 받은 thread가 해당 값에 접근한다면,
	// 올바른 계산을 보장할 수 없다.
	// 아래 함수는 어떤 연산을 하드웨어의 지원을 받아 원자적(더 쪼갤 수 없는 하나의 연산 또는 명령어)으로 수행하여
	// 값이 완전히 수정되고 저장되는 것을 보장한다.
	// lock을 거는게 아니라 하드웨어의 지원을 받아서 원자적으로 처리하는 것

	// mIsSending을 true와 비교해서 같다면, false로 교환하고
	// 반환값은 교환하기 전의 원래 값을 의미한다.

	// mIsSending이 true라면,
	// 앞서 송신한 바이트가 완전히 다 송신되었기 때문에 다음 송신을 진행해도 된다는 뜻이고
	// false라면,
	// 앞서 송신한 바이트가 완전히 다 송신되지 않았거나,
	// 완료 통지가 처리되지 않았다는 뜻이다.

	// 분기처리를 하는 이유는
	// WSASend()가 데이터를 전부 보낸다는 것을 확신할 수 없기 때문이다.
	// 예를 들어 1024바이트를 전송하라고 OS에 요청했을 때
	// 1000바이트만 전송되었다고 해보자.
	// 그런데 직후에 바로 500바이트 전송 요청이 있었고
	// 그 다음에 24바이트를 추가로 전송 요청을 한다면,
	// 수신하는 쪽에서는 1000 - 500 - 24 바이트 순서로 받게 되어
	// 패킷의 순서가 변경되고 데이터를 올바르게 해석할 수 없다.
	// 그래서 1000바이트를 보내고 24바이트가 남았다면, mIsSending 변수를 false로 둔 상태로
	// send 작업 완료 통지 내부에서 24바이트에 대해 WSASend()를 호출한다.
	// 결국 24바이트까지 완전히 전송되고 나면,
	// send 작업 완료 통지에서 mIsSending 변수를 true로 바꾸고
	// 다음 send 작업을 SendPost() 함수에서 수행할 수 있게된다.
	if (InterlockedCompareExchange64(
		reinterpret_cast<LONG64*>(&mIsSending),
		static_cast<unsigned long long>(false),
		static_cast<unsigned long long>(true)) == static_cast<unsigned long long>(true))
	{
		int realSendSize{ 0 };
		
		// 메모리에서 송신할 데이터의 시작 위치가 반환되며, realSendSize에는 송신 가능한 바이트 수가 담긴다.
		char* pBuf{ mSendRingBuffer.GetBuffer(mSendBufSize, &realSendSize) };

		// send ring buffer에 송신할 데이터가 없다
		if (nullptr == pBuf)
		{
			InterlockedExchange64(
				reinterpret_cast<LONG64*>(&mIsSending),
				static_cast<unsigned long long>(true));

			// 더 이상 send할 데이터가 없으니 false 반환
			return false;
		}

		// 지금까지 송신한 바이트 수
		// SendPost에서 WSASend()를 호출한다는 것은 새로운 패킷을 송신한다는 의미라서 0이고
		// WSASend()가 완료되어 작업 완료 통지에서 후처리를 하는데
		// 모든 데이터가 송신되지 않았음을 확인하면, 그 자리에서 WSASend()를 호출하고 그 때는 0이 아닌 이때까지 송신한 바이트 수
		mSendOverlappedEx->mProcessedBytes = 0;
		mSendOverlappedEx->mOperation = eOperationType::OP_SEND;

		// 총 송신해야 하는 바이트 수
		mSendOverlappedEx->mTotalBytes = realSendSize;

		ZeroMemory(&mSendOverlappedEx->mOverlapped, sizeof(mSendOverlappedEx->mOverlapped));
		mSendOverlappedEx->mWSABuf.buf = pBuf;
		mSendOverlappedEx->mWSABuf.len = realSendSize;
		mSendOverlappedEx->mConnection = this;

		// WSASend()를 호출하기 때문에
		// 잊지말고 출력 카운트를 올려주자
		IncrementSendIORefCount();

		DWORD numOfBytesSent{ 0 };
		int ret = WSASend(
			mClientSocket,
			&mSendOverlappedEx->mWSABuf,
			1,
			&numOfBytesSent,
			0,
			&mSendOverlappedEx->mOverlapped,
			NULL);

		if (SOCKET_ERROR == ret && WSA_IO_PENDING != WSAGetLastError())
		{
			DecrementSendIORefCount();

			IOCPServer::GetIOCPServer()->CloseConnection(this);

			LOG(eLogInfoType::LOG_ERROR_NORMAL,
				L"[ERROR] socket[%llu] WSAsend(): SOCKET_ERROR, %d",
				mClientSocket, WSAGetLastError());

			// WSASend() 실패했으니
			return false;
		}

		// 이쪽 if문을 타서 진입했다면,
		// mIsSending은 InterlockedCompareExchange() 함수 호출로 false로 바뀐 상태인데
		// 책에서는 이를 한번 더 변경하고 있다.
		// 이 코드는 제거해도 괜찮을 것 같다.
		InterlockedExchange64(
			reinterpret_cast<LONG64*>(&mIsSending),
			static_cast<unsigned long long>(false));

		// WSASend()를 호출했고
		// 요청이 정상적으로 진행
		//return false;
		return true;
	}

	// 앞서 보낸 데이터가 아직 전송중이기 때문에 false 반환
	//return true;
	return false;
}

bool Connection::BindAcceptExSock()
{
	memset(&mRecvOverlappedEx->mOverlapped, 0x00, sizeof(mRecvOverlappedEx->mOverlapped));

	// AcceptEx() 호출 후
	// 비동기로 client 요청 수락이 완료되면,
	// server 주소, client 주소 등과 같은 추가 데이터가 저장될 공간
	mRecvOverlappedEx->mWSABuf.buf = mAddressBuf;
	mRecvOverlappedEx->mPacketStart = &mRecvOverlappedEx->mWSABuf.buf[0];
	mRecvOverlappedEx->mWSABuf.len = sizeof(mAddressBuf);

	mRecvOverlappedEx->mOperation = eOperationType::OP_ACCEPT;
	mRecvOverlappedEx->mConnection = this;

	// Accept() 함수는 client의 연결 요청이 들어오면 반환하면서
	// 해당 유저와 통신하는 전용 client socket을 생성한다.
	// 하지만 AcceptEx() 함수는 client 연결 요청을 비동기로 처리하고
	// 이 때 미리 생성해둔 client socket을 인자로 넣어야 한다.
	mClientSocket = WSASocket(AF_INET,
		SOCK_STREAM,
		IPPROTO_TCP,
		NULL,
		0,
		WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == mClientSocket)
	{
		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Connection::BindAcceptExSock() | WSASocket() Failed: error[%d]",
			WSAGetLastError());

		return false;
	}

	// accept 비동기 IO 작업을 요청하기 때문에 카운트 1 증가.
	// 후에 IOCP queue에서 accpet 작업에 대한 완료 통지를 받으면, 카운트 1 감소.
	IncrementAcceptIORefCount();

	DWORD bytesReceived{ 0 };
	bool ret = AcceptEx(mListenSocket,
		mClientSocket,
		mRecvOverlappedEx->mWSABuf.buf,
		// 자동으로 송신되는 server 로컬 주소, client 원격 주소를 제외하고는
		// 아무 정보도 받지 않을 것이라서 0으로 세팅.
		// 그렇다면, client로부터 연결 요청이 들어오면,
		// 추가적인 데이터 수신을 위해 대기하지 않고 바로 작업이 완료되고
		// IOCP queue에 완료 작업이 추가됨
		0,
		sizeof(SOCKADDR_IN) + 16,
		sizeof(SOCKADDR_IN) + 16,
		&bytesReceived,
		// overlapped 구조체를 포함하여 추가 정보를 넣어서 확장한
		// OverlappedEx 구조체는
		// 가장 선두에 overlapped 구조체가 위치하고 있기 때문에
		// OverlappedEx 데이터의 시작 주소가 곧 overlapped 데이터 시작 주소이다.
		reinterpret_cast<LPOVERLAPPED>(mRecvOverlappedEx));

	if (!ret && WSA_IO_PENDING != WSAGetLastError())
	{
		DecrementAcceptIORefCount();

		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Connection::BindAcceptExSock() | AcceptEx() failed: error[%d]",
			WSAGetLastError());

		return false;
	}

	return true;
}

char* Connection::PrepareSendPacket(int sendLength)
{
	if (false == mIsConnected)
	{
		return nullptr;
	}

	// sendLength만큼의 버퍼를 확보
	char* pBuf = mSendRingBuffer.MoveMark(sendLength);
	if (nullptr == pBuf)
	{
		// IOCPServer의 CloseConnection()을 호출하면,
		// 연산 종류로 OP_CLOSE를 설정해서 PQCS() 함수를 호출하여
		// 순서성있게 처리하는 ProcessIOCP queue에 넣어준다.
		// ProcessThread는 작업 완료 통지를 꺼내서
		// Connection class의 CloseConnection()을 호출한다.
		IOCPServer::GetIOCPServer()->CloseConnection(this);

		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Connection::PrepareSendPacket() | Socket[%d] SendRingBuffer overflow",
			mClientSocket);

		return nullptr;
	}

	ZeroMemory(pBuf, sendLength);

	// 우리가 설계한 패킷의 프로토콜은
	// 선두 4바이트에 패킷의 길이를 넣게 되어있다.
	// 패킷 수신 완료 통지를 받고나서
	// 수신한 바이트의 길이가 PACKET_SIZE_LENGTH(4)바이트 보다 크다면,
	// 헤더를 수신한 것이고 헤더로부터 메시지 사이즈를 읽고
	// 수신한 바이트의 길이가 메시지 사이즈보다 크다면,
	// 온전한 하나의 패킷을 수신한 것이고
	// 이를 처리한다.
	CopyMemory(pBuf, &sendLength, PACKET_SIZE_LENGTH);

	return pBuf;
}

void Connection::SetSocket(SOCKET socket)
{
	mClientSocket = socket;
}

SOCKET Connection::GetSocket()
{
	return mClientSocket;
}

void Connection::SetConnectionIP(char* IP)
{
	memcpy(mClientIP, IP, sizeof(mClientIP));
}

char* Connection::GetConnectionIP()
{
	return mClientIP;
}

int Connection::GetIndex()
{
	return mIndex;
}

int Connection::GetRecvBufSize()
{
	return mRecvBufSize;
}

int Connection::GetSendBufSize()
{
	return mSendBufSize;
}

int Connection::GetRecvIORefCount()
{
	return mRecvIORefCount;
}

int Connection::GetSendIORefCount()
{
	return mSendIORefCount;
}

int Connection::GetAcceptIORefCount()
{
	return mAcceptIORefCount;
}

void Connection::IncrementRecvIORefCount()
{
	// Interlocked 계열 함수는 기본적으로 공유 자원에 대해 접근할 때 사용하는 것이 일반적이다.
	// 공유 자원이란 동일한 객체 내부의 멤버 변수, 전역 변수, 정적 변수와 같은 것을 말한다.
	// 지역 변수에도 이 함수를 적용할 수 있지만,
	// 지역 변수는 해당 함수를 수행하는 thread 혼자만 접근하는 것이기 때문에
	// 사용하는 것이 크게 의미가 없으며, 사용하려고 하면
	// 실제로 C28113 warning이 발생한다.
	InterlockedIncrement64(reinterpret_cast<LONG64*>(&mRecvIORefCount));
}

void Connection::IncrementSendIORefCount()
{
	InterlockedIncrement64(reinterpret_cast<LONG64*>(&mSendIORefCount));
}

void Connection::IncrementAcceptIORefCount()
{
	InterlockedIncrement64(reinterpret_cast<LONG64*>(&mAcceptIORefCount));
}

void Connection::DecrementRecvIORefCount()
{
	InterlockedDecrement64(reinterpret_cast<LONG64*>(&mRecvIORefCount));
}

void Connection::DecrementSendIORefCount()
{
	InterlockedDecrement64(reinterpret_cast<LONG64*>(&mSendIORefCount));
}

void Connection::DecrementAcceptIORefCount()
{
	InterlockedDecrement64(reinterpret_cast<LONG64*>(&mAcceptIORefCount));
}
