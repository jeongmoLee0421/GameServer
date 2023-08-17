#define _WINSOCKAPI_
#include <Windows.h>

#include "Monitor.h"

Monitor::Owner::Owner(Monitor& crit)
	: mSyncObject{ crit } // 참조자는 생성과 동시에 초기화
{
	mSyncObject.Enter();
}

Monitor::Owner::~Owner()
{
	mSyncObject.Leave();
}

Monitor::Monitor()
{
	InitializeCriticalSection(&mSyncObject);
}

Monitor::~Monitor()
{
	DeleteCriticalSection(&mSyncObject);
}

void Monitor::Enter()
{
	EnterCriticalSection(&mSyncObject);
}

void Monitor::Leave()
{
	LeaveCriticalSection(&mSyncObject);
}
