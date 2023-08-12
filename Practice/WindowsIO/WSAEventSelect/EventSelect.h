#pragma once

// 2023 08 12 이정모 home

// WSAEventSelect()를 사용한 서버
// 지정한 socket에 지정한 network event가 발생하는 경우
// socket과 연동된 event 객체의 상태를 signaled 상태로 변경

#define _WINSOCKAPI_
#include <Windows.h>

class EventSelectSocket;

class EventSelect
{
public:
	EventSelect();
	~EventSelect();

	HRESULT Initialize(HINSTANCE hInstance, int nCmdShow);
	HRESULT Finalize();

	void Loop();

	static LRESULT CALLBACK WinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
	HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow);

private:
	static constexpr int MAX_SOCKBUF = 1024;
	char mSocketBuf[MAX_SOCKBUF];
	EventSelectSocket* mEventSelectSocket;
	wchar_t mErrorMessage[256];

private:
	HINSTANCE mHInstance;
	HWND mHwnd;
	MSG mMsg;

	LONG mClientWidth;
	LONG mClientHeight;
};

