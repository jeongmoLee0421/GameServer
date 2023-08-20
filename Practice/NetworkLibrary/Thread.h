#pragma once

// 2023 08 20 이정모 home

// 일정 시간마다 특정 작업을 하기 위한 thread class
// 
// tick마다 client에게 패킷을 전송하여
// 해당 client가 연결되어있는지(살아있는지) 여부를 체크하거나
// 
// 온라인 게임 서버 동기화는 서버 시간을 기준으로 하는데
// server tick이란 단위를 만들어서 처리할 수도 있다.

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif

#define _WINSOCKAPI_
#include <Windows.h>

class NETLIB_API Thread
{
public:
	Thread();
	~Thread();

public:
	bool CreateThread(DWORD waitTick);
	void DestroyThread();
	void Run();
	void Stop();

public:
	// 한 주기(tick)가 지나면
	// 해야할 작업이 구현된 OnProcess() 함수를 호출해준다.
	void TickThread();

	// OnProcess() 함수를 순수 가상 함수로 선언해서
	// Thread class를 상속하는 모든 자식 class는 이를 반드시 재정의해야 함.
	// 
	// 결국 자식 thread마다 해야할 작업들이 다르게 재정의 될 것이고
	// 부모 thread로 Up Casting 하여 OnProcess() 함수를 호출하면,
	// 가상 함수 테이블을 참조하여
	// 자식 thread에서 구현된 OnProcess() 함수를 호출한다.
	virtual void OnProcess() = 0;

	// 주기가 몇번 반복되었나?
	DWORD GetTickCount();
	bool IsRunning();

	// 자식 class에서 사용할 필요가 있을 수도 있기에 protected
protected:
	HANDLE mThread;
	HANDLE mQuitEvent;

	bool mIsRunning;
	
	DWORD mWaitTick;
	DWORD mTickCount;
};