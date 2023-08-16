#pragma once

// 2023 08 16 이정모 home

// IO Completion Port 모델을 사용한 서버
// IOCP 객체를 생성한 뒤
// 이 객체와 연결된 소켓으로 Overlapped IO 작업을 요청하고
// Overlapped IO 작업이 완료되면,
// IOCP queue에 완료된 작업에 대한 정보가 추가된다.
// 우리는 이 queue에서 완료된 작업을 꺼내서
// 후 처리를 해준다.

#define _WINSOCKAPI_
#include <Windows.h>

class IOCompletionPort;

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

	IOCompletionPort* mIOCompletionPort;

private:
	HINSTANCE mHInstance;
	HWND mHwnd;
	MSG mMsg;

	LONG mClientWidth;
	LONG mClientHeight;
};

