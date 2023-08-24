#define _WINSOCKAPI_
#include <Windows.h>
#include <cassert>
#include <process.h>

#include "Thread.h"
#include "Log.h"

Thread::Thread()
	: mThread{ NULL }
	, mIsRunning{ false }
	, mWaitTick{ 0 }
	, mTickCount{ 0 }
{
	mQuitEvent = CreateEvent(
		NULL,
		true, // QuitEvent가 세팅되어 Signaled 상태가 되면, thread가 종료될거라서 굳이 자동 reset이 필요하지는 않다.
		false,
		NULL
	);

	// mQuitEvent는 참이라고 주장한다.
	// 그니까 NULL이면 std::abort가 호출되어 프로그램을 중단
	assert(mQuitEvent);
}

Thread::~Thread()
{
	if (mQuitEvent)
	{
		CloseHandle(mQuitEvent);
	}

	if (mThread)
	{
		CloseHandle(mThread);
	}
}

unsigned int WINAPI CallTickThread(LPVOID p)
{
	Thread* pThread = reinterpret_cast<Thread*>(p);

	pThread->TickThread();

	return 0;
}

bool Thread::CreateThread(DWORD waitTick)
{
	unsigned int threadID{ 0 };

	//mThread = reinterpret_cast<HANDLE>(_beginthreadex(
	//	NULL,
	//	0,
	//	CallTickThread,
	//	this, // this 포인터를 넘겨서 멤버 함수를 호출해준다.
	//	CREATE_SUSPENDED, // 이후에 내가 원하는 타이밍에 thread 진행 가능
	//	&threadID
	//));

	mThread = reinterpret_cast<HANDLE>(_beginthreadex(
		NULL,
		0,
		CallTickThread,
		this, // this 포인터를 넘겨서 멤버 함수를 호출해준다.
		0, // ResumeThread()에 문제가 많아서 사용x
		&threadID
	));

	if (NULL == mThread)
	{
		LOG(eLogInfoType::LOG_ERROR_NORMAL,
			L"SYSTEM | Thread::CreateThread() | TickThread 생성 실패: Error(%lu)",
			GetLastError());

		return false;
	}

	mWaitTick = waitTick;
	return true;
}

void Thread::DestroyThread()
{
	// event 객체를 signaled 상태로 변경하더라도
	// thread가 일시정지한 상태라면,
	// WaitForSingleObject() 함수 호출이 불가하여
	// event 객체의 상태를 감지할 수 없기 때문
	Run();

	// Quit Event를 signaled 상태로 변경하여
	// WaitForSingleObject() 함수에서 이를 감지하면,
	// 반환 값을 확인해서 while문 탈출하여 thread를 종료
	SetEvent(mQuitEvent);

	WaitForSingleObject(mThread, INFINITE);
}

void Thread::Run()
{
	if (false == mIsRunning)
	{
		mIsRunning = true;
		//ResumeThread(mThread);
	}
}

void Thread::Stop()
{
	// 모던 C++에 들어와서는
	// thread를 임의로 멈추거나 재개하는 함수를 제공하지 않았다.
	// 그 이유는 동기화를 위한 lock 소유 문제가 있다.
	//
	// 예를 들어서
	// 어떤 thread가 lock을 소유했는데
	// 하필 그 순간에 thread 정지 함수가 호출되어서
	// 아무런 작업을 하지 않는다면,
	// 추후에 thread 재개 함수를 호출하여 정지되어 있는 thread를 깨우기 전까지
	// lock을 필요로 하는 다른 thread도 작업 진행이 멈춰버리는 경우가 생긴다.
	//
	// 그래서 조건 변수 같은 것을 사용해서
	// 수식이 거짓인 경우에는 계속 대기하고 있다가
	// 수식이 참이면, 일어나서 추가 작업을 하는 등의 방법을
	// 요즘에는 권장하고 있다.

	if (true == mIsRunning)
	{
		mIsRunning = false;
		//SuspendThread(mThread);
	}
}

void Thread::TickThread()
{
	while (true)
	{
		DWORD ret = WaitForSingleObject(mQuitEvent, mWaitTick);

		// event 객체가 signaled 상태가 되었다
		if (WAIT_OBJECT_0 == ret)
		{
			break;
		}
		// 시간 제한이 경과했고
		// non-signaled 상태
		else if (WAIT_TIMEOUT)
		{
			++mTickCount;
			OnProcess();
		}
	}
}

DWORD Thread::GetTickCount()
{
	return mTickCount;
}

bool Thread::IsRunning()
{
	return mIsRunning;
}
