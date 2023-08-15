// 2023 08 14 이정모 home

// 게임 진입점

#define _WINSOCKAPI_
#include <Windows.h>

#include "GameProcess.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
	GameProcess* pGameProcess = new GameProcess{};

	if (FAILED(pGameProcess->Initialize(hInstance, nCmdShow)))
	{
		return -1;
	}

	pGameProcess->Loop();

	if (FAILED(pGameProcess->Finalize()))
	{
		return -1;
	}

	delete pGameProcess;

	return 0;
}