#include "GameProcess.h"
#include "OverlappedEvent.h"

GameProcess::GameProcess()
	: mHInstance{ nullptr }
	, mHwnd{ nullptr }
	, mMsg{}
	, mClientWidth{ 0 }
	, mClientHeight{ 0 }
	, mSocketBuf{ 0, }
	, mOverlappedEvent{ nullptr }
{
}

GameProcess::~GameProcess()
{
}

HRESULT GameProcess::Initialize(HINSTANCE hInstance, int nCmdShow)
{
	if (FAILED(InitWindow(hInstance, nCmdShow)))
	{
		return E_FAIL;
	}

	mOverlappedEvent = new OverlappedEvent{};
	mOverlappedEvent->InitSocket(mHwnd);
	mOverlappedEvent->BindAndListen(8000);
	mOverlappedEvent->StartServer();

	return S_OK;
}

HRESULT GameProcess::Finalize()
{
	HRESULT hr = S_OK;

	delete mOverlappedEvent;

	return hr;
}

void GameProcess::Loop()
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
		}
	}
}

LRESULT GameProcess::WinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

HRESULT GameProcess::InitWindow(HINSTANCE hInstance, int nCmdShow)
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