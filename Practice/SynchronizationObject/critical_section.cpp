// 2023 08 09 이정모 home

// 유저 모드 동기화 객체 critical_section
// 커널 모드로 전환해서 커널 메모리의 데이터를 수정하는 것이 아니기 때문에
// 가볍고 수행 속도가 좀 더 빠르다.

#include <Windows.h>
#include <iostream>
#include <process.h>
using namespace std;

int gNumber{ 0 };
CRITICAL_SECTION gCS{};

unsigned int __stdcall ThreadFunc(void*)
{
	// EnterCriticalSection()을 호출하면
	// gCS.RecursionCount가 1 증가하고
	// LeaveCriticalSection()을 호출하면
	// gCS.RecursionCount가 1 감소한다.
	// 위 두 함수를 짝지어서 잘 호출해야
	// recursion count가 0으로 유지되어
	// 다른 thread가 임계 구역에 진입할 수 있다.

	for (int i = 0; i < 100000; ++i)
	{
		EnterCriticalSection(&gCS);
		
		gNumber++;

		LeaveCriticalSection(&gCS);
	}

	return 0;
}

int main()
{
	InitializeCriticalSection(&gCS);

	unsigned int threadID{};

	HANDLE hThread1 = (HANDLE)_beginthreadex(
		nullptr,
		0,
		ThreadFunc,
		nullptr,
		CREATE_SUSPENDED, // thread를 생성하면서 곧바로 함수를 수행하지 않고 이후에 원하는 타이밍에 수행 가능
		&threadID
	);
	
	cout << "create thread id: " << threadID << endl;
	
	if (nullptr == hThread1)
	{
		cout << "thread 1 is nullptr" << endl;
	}

	HANDLE hThread2 = (HANDLE)_beginthreadex(
		nullptr,
		0,
		ThreadFunc,
		nullptr,
		CREATE_SUSPENDED,
		&threadID
	);
	
	cout << "create thread id: " << threadID << endl;

	if (nullptr == hThread2)
	{
		cout << "thread 2 is nullptr" << endl;
	}

	// 원하는 타이밍에 thread 진행
	ResumeThread(hThread1);
	ResumeThread(hThread2);

	// thread가 작업을 다 마치고
	// return할 때까지 기다림
	WaitForSingleObject(hThread1, INFINITE);
	WaitForSingleObject(hThread2, INFINITE);

	CloseHandle(hThread1);
	CloseHandle(hThread2);

	cout << "gNumber: " << gNumber << endl;

	DeleteCriticalSection(&gCS);
}