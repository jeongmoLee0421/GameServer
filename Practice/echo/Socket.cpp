#include <iostream>
#include <ws2tcpip.h>

#include "Socket.h"

#pragma comment(lib, "ws2_32")

Socket::Socket()
	: mSocket{ INVALID_SOCKET }
	, mConnectedSocket{ INVALID_SOCKET }
	, mSocketBuf{}
{
}

Socket::~Socket()
{
	WSACleanup();
}

bool Socket::InitSocket()
{
	WSAData wsaData;

	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (0 != ret)
	{
		std::cout << "[에러] 위치: Socket::InitSocket(), 이유: WSAStartup() 실패, error code: " << ret << std::endl;
		return false;
	}

	mSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == mSocket)
	{
		std::cout << "[에러] 위치: Socket::InitSocket(), 이유: socket() 실패, error code: " << WSAGetLastError() << std::endl;
		return false;
	}

	std::cout << "소켓 초기화 성공" << std::endl;
	return true;
}

void Socket::CloseSocket(SOCKET closeSocket, bool isForce)
{
	// closesocket()에서 블로킹되지 않으면서
	// 모든 데이터 송수신이 완료될때까지 기다렸다가
	// 우아하게 종료
	linger linger{ 0, 0 };

	// 강제 종료를 원한다.
	// 만약 현재 socket이 데이터를 전송하고 있을 떄
	// 강제 종료를 하게되면 일부 데이터는 전송되지 않음
	if (true == isForce)
	{
		linger.l_onoff = SO_LINGER;
	}

	// 입출력 스트림을 모두 닫는다.
	shutdown(closeSocket, SD_BOTH);

	// 강제 종료 여부를 소켓에 세팅
	setsockopt(closeSocket, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&linger), sizeof(linger));

	// 소켓 연결 종료 및
	// 아마도 커널 메모리에 있는 소켓 관련 메모리 정리
	closesocket(closeSocket);

	closeSocket = INVALID_SOCKET;
}

bool Socket::BindAndListen(int bindPort)
{
	SOCKADDR_IN serverAddr{};

	serverAddr.sin_family = AF_INET;

	// 시스템(intel, motorola)마다 메모리에 바이트를 저장하는 순서가 다르다.
	// 리틀 엔디안은 낮은 바이트가 메모리의 낮은 주소에 위치하고
	// 빅 엔디안은 높은 바이트가 메모리의 메모리의 낮은 주소에 위치한다.
	// 바이트 순서가 다른 시스템이 통신을 할 때 같은 데이터지만
	// 메모리에 올리는 순서가 달라서 다르게 해석되는 경우가 있다.
	// 이런 것을 방지하기 위해 네트워크 바이트 순서는 빅 엔디안이라 정했고
	// host to network short/long()의 함수를 사용해서
	// 호스트 바이트 순서를 네트워크 바이트 순서로 변경해서 전달한다.

	// 예를 들어서
	// 리틀 엔디안에서 3은 03 00으로 메모리에 저장된다.
	// 이를 그대로 송신하면 03이 가고 00이 간다.
	// 빅 엔디안에서 이를 받으면 자신의 메모리에 03 00이라 저장한다.
	// 빅 엔디안에서는 이를 768이라고 해석한다.
	// 그래서 리틀 엔디안에서 네트워크 바이트 순서로 변경해서 00을 보내고 03을 보내면
	// 빅 엔디안에서 00 03을 받고 본인이 빅 엔디안이기 때문에 바이트 순서를 변경하지 않을 것이고
	// 이는 리틀 엔디안과 같이 3으로 해석된다.
	serverAddr.sin_port = htons(bindPort);

	// server PC에 랜카드가 여러개일 수 있고
	// 랜카드마다 ip주소가 할당되는데
	// 어떤 랜카드로 들어오던지 다 받겠다는 뜻
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	// 지정한 주소와 포트로 넘어오는 패킷을 인자로 넘긴 socket으로 수신하겠다고
	// 운영체제에 등록하는 과정
	int ret = bind(mSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
	if (SOCKET_ERROR == ret)
	{
		std::cout << "[에러] 위치: Socket::BindAndListen(), 이유: bind() 실패, error code: " << WSAGetLastError() << std::endl;
		return false;
	}

	ret = listen(mSocket, 5);
	if (SOCKET_ERROR == ret)
	{
		std::cout << "[에러] 위치: Socket::BindAndListen(), 이유: listen() 실패, error code: " << WSAGetLastError() << std::endl;
		return false;
	}

	std::cout << "서버 등록 성공" << std::endl;
	return true;
}

bool Socket::StartServer()
{
	char outStr[1024];

	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(clientAddr);

	std::cout << "서버 시작" << std::endl;

	// client로부터 연결 요청이 들어오면 이를 수락하면서
	// 해당 client와 통신할 전용 socket 생성
	mConnectedSocket = accept(mSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
	if (INVALID_SOCKET == mConnectedSocket)
	{
		std::cout << "[에러] 위치: Socket::StartServer(), 이유: accept() 실패, error code: " << WSAGetLastError() << std::endl;
		return false;
	}

	// inet_addr()을 통해 문자열 주소를 정수형으로 넣었다.
	// 정수형을 다시 문자열로 복구하기 위해서는 inet_ntoa() 또는 inet_ntop 사용
	char strAddr[1024]{};
	sprintf_s(&outStr[0],
		sizeof(outStr),
		"클라이언트 접속: IP(%s) SOCKET(%llu)",
		inet_ntop(AF_INET, &clientAddr.sin_addr, &strAddr[0], sizeof(strAddr)),
		mConnectedSocket);
	std::cout << outStr << std::endl;

	while (true)
	{
		// TCP 통신이기 때문에 recv() 호출 한번에 데이터를 다 받을거라고 생각하면 안된다.
		// local에서 간단한 문자열 통신이라서 한번만 호출함
		int recvLen = recv(mConnectedSocket, &mSocketBuf[0], sizeof(mSocketBuf), 0);

		if (0 == recvLen)
		{
			std::cout << "클라이언트와 연결이 종료되었습니다." << std::endl;
			CloseSocket(mConnectedSocket, false);

			// 수신 그리고 송신에 문제가 있으면
			// server를 재귀적으로 다시 시작함
			// 문제가 계속해서 발생해서 스택 프레임이 쌓이면
			// 언젠가 스택 오버플로우가 발생할거임
			StartServer();
			return false;
		}
		else if (SOCKET_ERROR == recvLen)
		{
			std::cout << "[에러] 위치: Socket::StartServer(), 이유: recv() 실패, error code: " << WSAGetLastError() << std::endl;
			CloseSocket(mConnectedSocket, false);

			StartServer();
			return false;
		}

		// 문제없이 수신
		mSocketBuf[recvLen] = '\0';
		std::cout << "메시지 수신: 수신 byte[" << recvLen << "], 내용: [" << mSocketBuf << "]" << std::endl;

		int sendLen = send(mConnectedSocket, &mSocketBuf[0], recvLen, 0);
		if (SOCKET_ERROR == sendLen)
		{
			std::cout << "[에러] 위치: Socket::StartServer(), 이유: send() 실패, error code: " << WSAGetLastError() << std::endl;
			CloseSocket(mConnectedSocket, false);

			StartServer();
			return false;
		}

		std::cout << "메시지 송신: 송신 byte[" << sendLen << "], 내용: [" << mSocketBuf << "]" << std::endl;
	}

	// 이 server는 여기 구문을 탈 수는 없다.
	// while문을 계속 돌면서
	// 문자열이 수신되면 계속 송신해주고
	// 에러가 발생하면 다시 재귀적으로 StartServer()를 호출하기 때문
	CloseSocket(mConnectedSocket, false);
	CloseSocket(mSocket, false);

	std::cout << "서버 정상 종료" << std::endl;
	return true;
}

bool Socket::Connect(const char* ip, int port)
{
	SOCKADDR_IN serverAddr{};
	char outMessage[1024]{};

	serverAddr.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &serverAddr.sin_addr.s_addr);
	serverAddr.sin_port = htons(port);

	int ret = connect(mSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
	if (SOCKET_ERROR == ret)
	{
		std::cout << "[에러] 위치: Socket::Connect(), 이유: connect() 실패, error code: " << WSAGetLastError() << std::endl;
		return false;
	}

	std::cout << "접속 성공" << std::endl;

	while (true)
	{
		std::cout << ">>";
		std::cin >> outMessage;

		// stricmp()는 양쪽 문자열의 대소문자를 구분하지 않고 비교
		// 정상 종료
		if (0 == _stricmp(outMessage, "quit"))
		{
			break;
		}

		int sendLen = send(mSocket, outMessage, static_cast<int>(strlen(outMessage)), 0);
		if (SOCKET_ERROR == sendLen)
		{
			std::cout << "[에러] 위치: Socket::Connect(), 이유: send() 실패, error code: " << WSAGetLastError() << std::endl;
			return false;
		}
		
		std::cout << "메시지 송신: 송신 byte[" << sendLen << "], 내용: [" << outMessage << "]" << std::endl;

		int recvLen = recv(mSocket, mSocketBuf, sizeof(mSocketBuf), 0);
		if (0 == recvLen)
		{
			std::cout << "서버와 연결이 종료되었습니다." << std::endl;
			CloseSocket(mSocket, false);
			return false;
		}
		else if (SOCKET_ERROR == recvLen)
		{
			std::cout << "[에러] 위치: Socket::Connect(), 이유: recv() 실패, error code: " << WSAGetLastError() << std::endl;
			CloseSocket(mSocket, false);
			return false;
		}

		mSocketBuf[recvLen] = '\0';
		std::cout << "메시지 수신: 수신 byte[" << recvLen << "], 내용: [" << mSocketBuf << "]" << std::endl;
	}

	CloseSocket(mSocket, false);
	std::cout << "클라이언트 정상 종료" << std::endl;
	return true;
}