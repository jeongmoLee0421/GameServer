#pragma once

// 2023 08 11 이정모 home

// socket에 어떤 network event가 발생하면
// 그에 맞는 메시지를 윈도우 메시지 큐에 넣어주는
// WSAASyncSelect()

#define _WINSOCKAPI_
#include <Windows.h>

class AsyncSocket;

class AsyncSelect
{
public:
	AsyncSelect();
	~AsyncSelect();

	HRESULT Initialize(HINSTANCE hInstance, int nCmdShow);
	HRESULT Finalize();

	void Loop();

	static LRESULT CALLBACK WinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
	HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow);
	LRESULT OnSocketMessage(WPARAM wParam, LPARAM lParam);

private:
	static constexpr int MAX_SOCKBUF = 1024;
	char mSocketBuf[MAX_SOCKBUF];
	AsyncSocket* mAsyncSocket;
	static AsyncSelect* mAsyncSelectInstance;
	wchar_t mErrorMessage[256];

private:
	HINSTANCE mHInstance;
	HWND mHwnd;
	MSG mMsg;

	LONG mClientWidth;
	LONG mClientHeight;
};

