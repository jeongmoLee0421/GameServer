#pragma once

// 2023 08 25 이정모 home

// client의 연결 정보를 나타내는 class
// client에게 데이터를 수신하기 위해 WSARecv()를 호출하고
// client에게 데이터를 송신하기 위해 WSASend()를 호출한다.

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif

#include <WinSock2.h>

#include "RingBuffer.h"
#include "Monitor.h"

// connection class 초기화를 위한 구성 정보
struct InitConfig
{
	// connection manager가
	// connection pool을 만들어서 객체를 미리 생성해둔다.
	// 자료구조로 배열을 사용하는데 배열의 인덱스이다.
	int mIndex;

	// 연결 요청을 받을 server socket
	SOCKET mListenSocket;

	// recv ring buffer size = recvBufCnt * recvBufSize
	// send ring buffer size = sendBufCnt * sendBufSize
	int mRecvBufCnt;
	int mSendBufCnt;

	// 데이터를 한번에 송수신할 수 있는 크기로
	// 한번에 송신하는 크기와 한번에 수신하는 크기를 분리하여 관리함으로써
	// server 상황에 맞게 유동적으로 조절
	int mRecvBufSize;
	int mSendBufSize;

	// 순서성 있게 처리해야하는 패킷의 최대 수.
	// process IOCP가 순서성 있는 작업을 처리하는데 처리할 수 있는 최대치를 정해둔 것이다.
	// process IOCP queue에 추가할 때 1 감소하고 작업 완료 통지를 꺼내서 후처리가 끝나면 1 증가한다.
	// 최대치를 정한 이유가 뭘까 생각해봤는데
	// 순서성 있게 작업을 처리해야 하기 때문에 Worker Thread와는 달리 Process Thread는 1개만 생성해야 한다.
	// 작업하는 스레드가 1개다보니 너무 많은 작업을 추가하면 과부하가 걸린다....?
	int mProcessPacketCnt;

	int mServerPort;

	// client 접속, 데이터 송수신 뒤처리 하는 thread
	int mWorkerThreadCnt;

	// 순서성이 있는, 동시에 진행되면 안되는 작업을 처리하는 thread
	int mProcessThreadCnt;

	InitConfig()
	{
		ZeroMemory(this, sizeof(InitConfig));
	}
};

// Overlapped IO 작업의 종류
enum class eOperationType
{
	OP_SEND,
	OP_RECV,

	// AcceptEx() 함수를 사용하면,
	// 함수 호출 당시에 client의 연결 요청이 없더라도 즉시 반환된다.
	// IOCP 객체에 listen socket을 연결해두고
	// listen socket에 연결 요청이 들어와서 접속을 수락하면, OS가 IOCP queue에 작업 완료 통지를 넣어주고
	// 우리는 그에 대한 후처리
	// (새로운 client도 IOCP 객체에 연결, 데이터 수신을 위한 connection class의 RecvPost() 함수 호출 등)
	// 를 진행한다.
	OP_ACCEPT,
};

// Overlapped IO 작업을 진행하기 위한 Overlapped 구조체와
// 부가적인 정보를 추가한 확장 구조체
struct OVERLAPPED_EX
{
	WSAOVERLAPPED mOverlapped;
	WSABUF mWSABuf;

	// 총 송신해야하는 바이트 수를 말한다.
	// 이 변수가 필요한 이유는
	// WSAsend()를 호출한 뒤 작업 완료 통지를 꺼내 후처리를 하는데
	// 요청한 데이터가 모두 송신되지 않고 일부만 송신되었다면,
	// 송신 요청 바이트 - 실제 송신 바이트를 계산하여
	// 다시 WSASend() 요청을 하기 위함이다.
	int mTotalBytes;

	// TCP 특성 상
	// 데이터를 여러번에 걸쳐 수신하거나 송신할 수도 있다.
	// 한 패킷을 온전히 수신하지 못했을 때, 그 패킷을 몇 바이트까지 받았는지?
	// 요청한 크기의 데이터를 전부 송신하지 못했을 때, 실제로 송신된 바이트가 얼마인지?
	DWORD mProcessedBytes;

	// 패킷의 첫 위치를 기억하는 포인터 변수
	// 한 패킷을 온전히 받으면, 이 위치를 시작으로 처리
	// 패킷의 시작 위치를 알아야 해석이 가능
	char* mPacketStart;

	// Overlapped IO 연산 종류
	eOperationType mOperation;

	// WSARecv() 작업이 완료되고 후처리 마지막에
	// client로부터 전송된 데이터를 계속해서 수신해야 하기 때문에
	// RecvPost()를 호출할 때 connection class가 필요하다.
	// 마찬가지로 WSASend()도 send ring buffer에 송신할 데이터가 있다면,
	// 계속해서 송신해야 하기 때문에 SnedPost()를 호출해야 하고
	// 역시 connection class가 필요하다.
	// GQCS() 함수에서 작업 완료 통지를 꺼낼 때
	// Overlapped IO 작업을 요청할 때 사용했던 Overlapped IO 구조체의 포인터를 가져올 수 있고
	// 그것을 OVERLAPPED_EX구조체로 캐스팅하면 connection class를 복구할 수 있다.
	void* mConnection;

	OVERLAPPED_EX(void* pConnection)
	{
		ZeroMemory(this, sizeof(OVERLAPPED_EX));
		mConnection = pConnection;
	}
};

class NETLIB_API Connection
{
public:
	Connection();
	~Connection();

public:
	// connection이 종료되면(client가 접속 종료하면),
	// connection 메모리를 해제하는 것이 아니라
	// 기본값으로 다시 세팅
	void InitializeConnection();

	// Connection Manager가 Connection 객체를 생성한 후에
	// 구성 정보를 참조하여 데이터를 세팅하고
	// BindAcceptExSock() 호출하여 client의 접속 요청을 받을 준비
	bool CreateConnection(InitConfig& initConfig);

	// client와의 연결을 끊고
	// InitializeConnection()을 호출해서 정보를 모두 초기화하고
	// BindAcceptExSock()을 호출해서 새로운 client를 받을 준비
	bool CloseConnection(bool isForce = false);

	// client의 연결 요청을 수락하고 나서
	// client socket에 대한 비동기 입출력 완료를
	// IOCP queue에 넣고 후 처리하기 위해
	// IOCP 객체에 client socket을 연결한다.
	bool BindIOCP(HANDLE hIOCP);

	// recv ring buffer에 수신할 공간을 마련하고 WSARecv()를 호출하는 함수다.
	// 한가지 고려할 점이,
	// 어떤 패킷은 하나의 완성된 패킷으로 받지 못하고 일부가 잘려서 받게 되는데
	// 그 패킷의 시작 위치를 pPacketStart로
	// 그 패킷을 완성하기 위해 얼마만큼 받았는지를 processedBytes로 기억해두고
	// 다음에 수신할 버퍼의 메모리 위치를 계산해야 한다.
	bool RecvPost(char* pPacketStart, DWORD processedBytes);

	// send ring buffer에 송신할 데이터가 저장되어 있을텐데
	// ring buffer.GetBuffer() 함수를 호출하여,
	// 송신할 버퍼의 시작 위치와 송신할 버퍼의 크기를 알아와서
	// WSASend()를 호출하여 데이터를 송신한다.
	bool SendPost();

	// AcceptEx() 함수를 호출해서 client 접속 요청을 비동기로 처리한다.
	// client 접속이 수락되면, Worker IOCP queue에 작업 완료 통지가 추가된다.
	bool BindAcceptExSock();

	// 송신할 데이터를 저장하기 공간을 마련하기 위해서
	// send ring buffer에 sendLength 크기만큼의 버퍼를 확보하라고 요청
	char* PrepareSendPacket(int sendLength);

public:
	void SetSocket(SOCKET socket);
	SOCKET GetSocket();

	// client 요청 수락이 비동기적으로 완료되면,
	// GetAcceptExSockaddrs() 함수 호출을 통해 client address를 알 수 있고
	// Connection class에 세팅해준다.
	void SetConnectionIP(char* IP);
	char* GetConnectionIP();

	int GetIndex();

	int GetRecvBufSize();
	int GetSendBufSize();

	int GetRecvIORefCount();
	int GetSendIORefCount();
	int GetAcceptIORefCount();

	// recv, send, accept IO의 작업 횟수를 세는 이유
	// OS에 overlapped IO가 요청된 상태에서 socket 연결이 끊긴다면,
	// 처리중인 overlapped IO는 중단되고 IOCP queue에 추가된 뒤, 실패로 반환된다.
	// 이 때 여러 작업이 중단되면, 이 작업들 모두 IOCP queue에 들어가는데
	// 만약 첫번째 반환된 실패 작업에 대한 후처리에서 
	// 연결이 끊긴 socket의 connection 객체를 재사용하기 위해 초기화를 진행했고
	// 직후에 새로운 client가 접속해서 연결을 했다고 해보자.
	// 이어서 두번째 반환된 실패 작업에 대한 후처리에서는
	// 새로운 client가 접속했는지 여부를 판단할 방법이 없어서 connection 객체를 다시 초기화한다.
	// client와 연결이 유지되어 있는 connection class를 초기화하는 것은 연결되어 있는 socket 정보를 지우는 것이고
	// 결국 socket은 연결되어 있지만, 데이터 송수신을 할 수 없다.
	// 그래서 overlapped IO 작업 횟수를 모두 세다가 마지막 작업 실패를 반환했을 때 작업 횟수가 0이 되면
	// 그 때 비로소 connection class에 대한 초기화를 진행한다.

	void IncrementRecvIORefCount();
	void IncrementSendIORefCount();
	void IncrementAcceptIORefCount();

	void DecrementRecvIORefCount();
	void DecrementSendIORefCount();
	void DecrementAcceptIORefCount();

public:
	Connection(const Connection& rhs) = delete;
	Connection(Connection&& rhs) = delete;

	Connection& operator=(const Connection& rhs) = delete;
	Connection& operator=(Connection&& rhs) = delete;

public:
	// Overlapped IO 요청을 위한 변수.
	// recv 전용, send 전용 변수를 따로 생성한 이유는
	// operation을 OP_RECV로 하여 WSARecv()를 호출하고
	// 같은 변수에 operation을 OP_SEND로 하여 WSASend()를 호출하면,
	// WSARecv()가 완료되어 후 처리를 진행할 떄
	// operation 값이 OP_SEND로 되어있어 send의 후처리를 하는 문제가 발생할 수 있기 때문
	OVERLAPPED_EX* mRecvOverlappedEx;

	// Overlapped 변수가 하나씩만 있는 이유는
	// 작성 중인 코드 구조가
	// WSARecv()가 온전히 완료되면, 작업 통지를 받아서 다음 WSARecv()를 호출하고
	// WSASend()가 온전히 완료되면, 작업 통지를 받아서 다음 WSASend()를 호출하기 때문이다.
	// 즉, 모든 데이터가 순서있게 처리되기 떄문이다.
	// 만약 순서성이 없는 데이터를 병렬적으로 처리하고 싶다면,
	// 추가적인 Overlapped 구조체를 동적으로 할당 받아서 처리하고 delete를 해야한다고 생각한다.
	// 같은 Overlapped 구조체를 사용하면, 늦게 요청한 Overlapped IO 데이터로 값이 덮어 써지고
	// 앞서 요청한 Overlapped IO의 후처리 과정에서는 덮어 써진 데이터를 참조하기 때문에 문제가 발생한다.
	OVERLAPPED_EX* mSendOverlappedEx;

	RingBuffer mRecvRingBuffer;
	RingBuffer mSendRingBuffer;

	// AcceptEx() 함수 호출 후 client 접속 요청을 비동기로 받으면,
	// OS가 IOCP queue에 작업 완료 통지를 넣고
	// Worker Thread가 IOCP queue에서 완료 통지를 꺼낸 뒤
	// GetAcceptExSockAddrs() 함수를 호출해서
	// mAddressBuf에 server의 local 주소와 client의 remote 주소를 저장한다.
	char mAddressBuf[1024];

	// client가 접속을 종료했는지 여부로
	// client와 통신에 문제가 발생하여 연결을 끊으면,
	// IOCPServer의 CloseConnection()이 호출되고
	// 여기서 mIsClosed 변수가 false인지 확인하여, 처리하고
	// true로 값을 변경한다.
	bool mIsClosed;

	// client와 통신하는 WSARecv(), WSASend() 함수를 호출할 떄
	// client와 연결되어 있는지 여부를 판단
	bool mIsConnected;

	// 하나의 패킷에 대해서 overlapped send가 진행중인지 여부를 판단하는 변수.
	// 예를 들어 WSASend()를 호출해서 1024바이트를 전송을 요청했다고 해보자.
	// 그런데 실제로는 1000바이트만 전송되었고
	// 그 사이에 동일한 client에 WSASend()가 또 호출되어 512바이트가 전송되었다.
	// 그리고 나는 나머지 24바이트를 전송했다.
	// 그럼 실제 전송된 데이터는 1000바이트 - 512바이트 - 24바이트가 된다.
	// 데이터 순서가 바뀌어 전송되었으니 client에서는 패킷 분석에 문제가 발생한다.
	// 그래서 송신 작업 완료 통지를 받고나서
	// 요청한 바이트가 모두 송신되지 않았다면, 이 값을 false로 유지한 상태에서 WSASend()를 호출하고
	// 온전히 모든 패킷이 송신되었음을 확인하면, 이 값을 true로 바꿔서 SendPost()를 호출한다.
	bool mIsSending;

private:
	SOCKET mClientSocket;
	SOCKET mListenSocket;

	// 한번에 송수신 가능한 데이터의 최대 크기
	int mRecvBufSize;
	int mSendBufSize;

	// Log.h에 MAX_IP_LENGTH 정의되어 있음
	// 추후에 define, 상수 등 공통으로 사용되는 것들은
	// 모아두어야겠음.
	// AcceptEx() 작업 완료 통지를 꺼내서 후처리할 때
	// local server 주소와 remote client 주소를 mAddressBuf에 저장하고
	// 후에 SetConnectionIP()를 호출해서 mClientIP에 remote client 주소를 저장한다.
	char mClientIP[MAX_IP_LENGTH];
	int mIndex;

	// Connection Manager에서 Connection 추가 및 삭제와 같은 작업이 발생할 떄
	// 다른 스레드들이 Connection을 관리하는 컨테이너에 접근하지 못하도록
	// lock을 걸기 위함
	// CloseConnection() 함수에서 IOCPServer의 OnClose() 함수를 호출하는데
	// OnClose 내부에서 Connection Manager가 관리하는 Connection 객체 컨테이너에서
	// 연결을 끊고자 하는 Connection 객체의 데이터를 삭제하는 과정이 있는데
	// 이런 상황에서 사용한다.
	Monitor mConnectionSyncObj;

	// 새롭게 연결된 client에 대해서
	// Overlapped IO 요청을 하고 완료 통지를 받아야하기 때문에
	// Worker IOCP 객체와 연결한다.
	HANDLE mIOCP;

	// overlapped IO 작업의 횟수를 카운팅한다.
	// 모든 작업이 완료되어 모든 횟수가 0이되면,
	// 그 때 connection class를 초기화하고 새로운 client를 받을 준비를 한다.
	DWORD mSendIORefCount;
	DWORD mRecvIORefCount;
	DWORD mAcceptIORefCount;
};