#define _WINSOCK_DEPRECATED_NO_WARNINGS // WSAAsyncSelect()
#include "AsyncSelect.h"
#include "AsyncSocket.h"

AsyncSelect* AsyncSelect::mAsyncSelectInstance{ nullptr };

AsyncSelect::AsyncSelect()
	: mHInstance{ nullptr }
	, mHwnd{ nullptr }
	, mMsg{}
	, mClientWidth{ 0 }
	, mClientHeight{ 0 }
	, mSocketBuf{ 0, }
	, mAsyncSocket{ nullptr }
	, mErrorMessage{ 0, }
{
}

AsyncSelect::~AsyncSelect()
{
}

HRESULT AsyncSelect::Initialize(HINSTANCE hInstance, int nCmdShow)
{
	if (FAILED(InitWindow(hInstance, nCmdShow)))
	{
		return E_FAIL;
	}

	mAsyncSocket = new AsyncSocket{};
	mAsyncSocket->InitSocket(mHwnd);
	mAsyncSocket->BindAndListen(8000);

	// WSAAyncSelect()를 호출해서
	// 소켓에 네트워크 이벤트가 발생하면
	// 윈도우 메시지 큐에 메시지를 전달하도록
	mAsyncSocket->StartServer();

	// 윈도우 메시지를 처리할 때
	// WinProc에서 OnSocketMessage()를 호출해야 하는데
	// WinProc이 static 멤버 함수라서 this포인터 없이 동작하기 떄문에
	// AsyncSelect 변수를 static으로 하나 더 만들어서
	// WinProc 내부에서 OnSocketMessage()를 호출가능하도록 함
	mAsyncSelectInstance = this;

	return S_OK;
}

HRESULT AsyncSelect::Finalize()
{
	HRESULT hr = S_OK;

	delete mAsyncSocket;

	return hr;
}

void AsyncSelect::Loop()
{
	while (true)
	{
		if (PeekMessage(&mMsg, NULL, 0, 0, PM_REMOVE))
		{
			if (mMsg.message == WM_QUIT) break;

			TranslateMessage(&mMsg);
			DispatchMessage(&mMsg);
		}
		else
		{
			//Update();
			//Render();
		}
	}
}

LRESULT AsyncSelect::WinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_SOCKETMSG:
		// WinProc은 static 함수라서
		// static 변수로 함수 호출
		mAsyncSelectInstance->OnSocketMessage(wParam, lParam);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

HRESULT AsyncSelect::InitWindow(HINSTANCE hInstance, int nCmdShow)
{
	const wchar_t CLASS_NAME[]{ L"WindowsIO" };

	WNDCLASS wc{};
	wc.style = WS_OVERLAPPED;
	wc.lpfnWndProc = WinProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	mHwnd = CreateWindow(CLASS_NAME, CLASS_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

	if (mHwnd == NULL)
	{
		return E_FAIL;
	}

	ShowWindow(mHwnd, nCmdShow);

	RECT rc;
	GetClientRect(mHwnd, &rc);
	mClientWidth = rc.right - rc.left;
	mClientHeight = rc.bottom - rc.top;

	return S_OK;
}

LRESULT AsyncSelect::OnSocketMessage(WPARAM wParam, LPARAM lParam)
{
	SOCKET socket = static_cast<SOCKET>(wParam);

	// 4바이트 중 상위 2바이트가 error code
	int error = WSAGETSELECTERROR(lParam);
	if (0 != error)
	{
		wsprintf(mErrorMessage,
			L"WSAGETSELECTERROR: %d",
			error);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		return false;
	}

	// 4바이트 중 하위 2바이트가 발생한 이벤트
	int _event = WSAGETSELECTEVENT(lParam);
	switch (_event)
	{
		// client와 통신하는 전용 socket.
		// client로부터 데이터가 전송되어
		// 수신버퍼에 데이터가 쌓였을 때 발생
	case FD_READ:
	{
		int recvLen = recv(socket,
			mSocketBuf,
			sizeof(mSocketBuf),
			0);

		if (0 == recvLen)
		{
			wsprintf(mErrorMessage,
				L"클라이언트와 연결이 종료되었습니다.");

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			return false;
		}
		else if (SOCKET_ERROR == recvLen)
		{
			wsprintf(mErrorMessage,
				L"recv() 에러: %d",
				WSAGetLastError());

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			mAsyncSocket->CloseSocket(socket);

			return false;
		}

		mSocketBuf[recvLen] = '\0';

		int sendLen = send(
			socket,
			mSocketBuf,
			recvLen,
			0
		);
		if (SOCKET_ERROR == sendLen)
		{
			wsprintf(mErrorMessage,
				L"send() 에러: %d",
				WSAGetLastError());

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			mAsyncSocket->CloseSocket(socket);

			return false;
		}

		break;
	}

	// server의 listen socket.
	case FD_ACCEPT:
	{
		SOCKADDR_IN clientAddr{};
		int addrLen = sizeof(clientAddr);

		SOCKET clientSocket = accept(
			socket,
			reinterpret_cast<sockaddr*>(&clientAddr),
			&addrLen
		);

		if (INVALID_SOCKET == clientSocket)
		{
			wsprintf(mErrorMessage,
				L"accept() 에러: %d",
				WSAGetLastError());

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			return false;
		}

		// 새로 생성된 client socket도
		// 이벤트가 발생하면 메시지를
		// 윈도우 메시지 큐에 전송하도록 등록
		int ret = WSAAsyncSelect(
			clientSocket,
			mHwnd,
			WM_SOCKETMSG,
			FD_READ | FD_CLOSE // client socket은 수신 이벤트와 접속 종료 이벤트를 감지
		);

		if (SOCKET_ERROR == ret)
		{
			wsprintf(mErrorMessage,
				L"WSAAsyncSelect() 에러: %d",
				WSAGetLastError());

			MessageBox(NULL,
				mErrorMessage,
				NULL,
				MB_OK);

			return false;
		}

		break;
	}

	case FD_CLOSE:
	{
		wsprintf(mErrorMessage,
			L"client 접속 종료: SOCKET(%llu)",
			socket);

		MessageBox(NULL,
			mErrorMessage,
			NULL,
			MB_OK);

		mAsyncSocket->CloseSocket(socket);

		break;
	}

	default:
		break;
	}

	return true;
}
