#include "EventSelect.h"
#include "EventSelectSocket.h"

EventSelect::EventSelect()
	: mHInstance{ nullptr }
	, mHwnd{ nullptr }
	, mMsg{}
	, mClientWidth{ 0 }
	, mClientHeight{ 0 }
	, mSocketBuf{ 0, }
	, mEventSelectSocket{ nullptr }
	, mErrorMessage{ 0, }
{
}

EventSelect::~EventSelect()
{
}

HRESULT EventSelect::Initialize(HINSTANCE hInstance, int nCmdShow)
{
	if (FAILED(InitWindow(hInstance, nCmdShow)))
	{
		return E_FAIL;
	}

	mEventSelectSocket = new EventSelectSocket{};
	mEventSelectSocket->InitSocket();
	mEventSelectSocket->BindAndListen(8000);
	mEventSelectSocket->StartServer();

	return S_OK;
}

HRESULT EventSelect::Finalize()
{
	HRESULT hr = S_OK;

	delete mEventSelectSocket;

	return hr;
}

void EventSelect::Loop()
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

LRESULT EventSelect::WinProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

HRESULT EventSelect::InitWindow(HINSTANCE hInstance, int nCmdShow)
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