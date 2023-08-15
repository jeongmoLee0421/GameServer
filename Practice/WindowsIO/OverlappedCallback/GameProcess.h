#pragma once

// 2023 08 14 이정모 home

// Callback 함수를 사용한 Overlapped IO
// Overlapped IO가 완료되면,
// WSASend(), WSARecv() 함수 호출 때 넘긴 함수 포인터를 통해
// 운영체제가 그 함수를 직접 호출해준다.

#define _WINSOCKAPI_
#include <Windows.h>

class OverlappedCallback;

class GameProcess
{
public:
	GameProcess();
	~GameProcess();

	HRESULT Initialize(HINSTANCE hInstance, int nCmdShow);
	HRESULT Finalize();

	void Loop();

	static LRESULT CALLBACK WinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
	HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow);

private:
	static constexpr int MAX_SOCKBUF = 1024;
	char mSocketBuf[MAX_SOCKBUF];

	OverlappedCallback* mOverlappedCallback;

private:
	HINSTANCE mHInstance;
	HWND mHwnd;
	MSG mMsg;

	LONG mClientWidth;
	LONG mClientHeight;
};

