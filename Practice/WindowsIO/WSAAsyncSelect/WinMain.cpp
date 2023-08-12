// 2023 08 11 이정모 home

// 게임 진입점

#include <Windows.h>

#include "AsyncSelect.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	AsyncSelect* pAsyncSelect = new AsyncSelect{};

	if (FAILED(pAsyncSelect->Initialize(hInstance, nCmdShow)))
	{
		return -1;
	}

	pAsyncSelect->Loop();

	if (FAILED(pAsyncSelect->Finalize()))
	{
		return -1;
	}

	delete pAsyncSelect;

	return 0;
}