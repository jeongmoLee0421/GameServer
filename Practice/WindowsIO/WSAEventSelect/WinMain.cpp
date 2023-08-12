// 2023 08 12 이정모 home

// 게임 진입점

#include <Windows.h>

#include "EventSelect.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	EventSelect* pEventSelect = new EventSelect{};

	if (FAILED(pEventSelect->Initialize(hInstance, nCmdShow)))
	{
		return -1;
	}

	pEventSelect->Loop();

	if (FAILED(pEventSelect->Finalize()))
	{
		return -1;
	}

	delete pEventSelect;

	return 0;
}