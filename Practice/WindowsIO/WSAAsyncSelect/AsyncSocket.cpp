#define _WINSOCK_DEPRECATED_NO_WARNINGS // WSAAsyncSelect()
#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>

#include <iostream>

#include "AsyncSocket.h"

#pragma comment(lib, "ws2_32")

AsyncSocket::AsyncSocket()
	: mListenSocket{ INVALID_SOCKET }
	, mHwnd{ nullptr }
{
}

AsyncSocket::~AsyncSocket()
{
	WSACleanup();
}

bool AsyncSocket::InitSocket(HWND hWnd)
{
	WSAData wsaData{};
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (0 != ret)
	{
		std::cout << "WSAStartUp() 실패: " << ret << std::endl;
		return false;
	}

	// 연결 지향형 socket 생성
	mListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == mListenSocket)
	{
		std::cout << "socket() 실패: " << WSAGetLastError() << std::endl;
		return false;
	}

	// newtwork event가 발생하면
	// message를 받을 window의 handle
	mHwnd = hWnd;

	std::cout << "소켓 초기화 성공" << std::endl;

	return true;
}

void AsyncSocket::CloseSocket(SOCKET closeSocket, bool isForce)
{
	// 우아한 종료 + non-blocking
	// 우아한 종료란
	// 보낼 데이터가 남아있다면 send()를 완료하고
	// 받을 데이터가 있으면 recv()를 완료하고 나서야
	// 소켓을 닫는다는 뜻
	struct linger _linger{ 0,0 };

	if (true == isForce)
	{
		_linger.l_onoff = 1;
	}

	// 송수신 stream 닫기
	shutdown(closeSocket, SD_BOTH);

	// linger 옵션 설정
	setsockopt(
		closeSocket,
		SOL_SOCKET,
		SO_LINGER,
		reinterpret_cast<char*>(&_linger),
		sizeof(_linger)
	);

	// socket이 우아한 종료를 수행한다면
	// send(), recv()가 완료될 때까지 socket을 닫지 않는다.
	// linger의 두번째 값인 l_linger(linger time)이 0이라면
	// non blocking으로 동작하여 closesocket()이 반한되고 다음 코드로 진행
	closesocket(closeSocket);

	closeSocket = INVALID_SOCKET;
}

bool AsyncSocket::BindAndListen(int bindPort)
{
	SOCKADDR_IN serverAddr{};
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(bindPort);
	// 랜 카드가 여러개라서 ip주소가 여러개라면
	// 모든 ip주소로 들어오는 데이터를 수신하겠다는 의미
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	int ret = bind(mListenSocket,
		reinterpret_cast<sockaddr*>(&serverAddr),
		sizeof(serverAddr));

	if (SOCKET_ERROR == ret)
	{
		std::cout << "bind() 실패: " << WSAGetLastError() << std::endl;
		return false;
	}

	ret = listen(mListenSocket, 5);
	if (SOCKET_ERROR == ret)
	{
		std::cout << "listen() 실패: " << WSAGetLastError() << std::endl;
		return false;
	}

	std::cout << "서버 등록 성공" << std::endl;
	return true;
}

bool AsyncSocket::StartServer()
{
	int ret = WSAAsyncSelect(
		mListenSocket, // 이 socket의 event 발생 여부 확인
		mHwnd, // 지정한 event가 발생했을 경우 message를 전달할 윈도우 핸들
		WM_SOCKETMSG, // 네트워크 이벤트가 발생했을 때 윈도우에 보낼 메시지
		FD_ACCEPT | FD_CLOSE // server 소켓은 accpet 이벤트를 감지해야함
	);

	if (SOCKET_ERROR == ret)
	{
		std::cout << "WSAAsyncSelect() 실패: " << WSAGetLastError() << std::endl;
		return false;
	}

	return true;
}
