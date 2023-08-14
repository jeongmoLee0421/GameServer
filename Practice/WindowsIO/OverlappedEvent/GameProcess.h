#pragma once

// 2023 08 14 이정모 home

// event 객체를 사용한 Overlapped IO
// IO 작업을 요청하고 백그라운드에서 실행되는 동안
// 다른 작업을 처리할 수 있음(또다른 IO작업 요청 가능)
// IO 작업이 완료되면 OS가 event 객체 상태를 signaled 상태로 변경하고
// 이를 감지하여 처리

#define _WINSOCKAPI_
#include <Windows.h>

class OverlappedEvent;

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

	OverlappedEvent* mOverlappedEvent;

private:
	HINSTANCE mHInstance;
	HWND mHwnd;
	MSG mMsg;

	LONG mClientWidth;
	LONG mClientHeight;
};

