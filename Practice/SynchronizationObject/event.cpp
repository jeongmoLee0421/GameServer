// 2023 08 09 이정모 home

// 커널 모드 동기화 객체 event
// event 객체 상태가 signaled인지 non-signaled인지 여부에 따라
// 코드의 흐름을 제어할 수 있음

#include <Windows.h>
#include <iostream>
#include <process.h>
using namespace std;

int gNumber{ 0 };
HANDLE hEvent{ nullptr };

unsigned int __stdcall ThreadFunc(void*)
{
	for (int i = 0; i < 100000; ++i)
	{
		// event가 signaled 상태가 되면 진입
		WaitForSingleObject(hEvent, INFINITE);

		gNumber++;

		// 본인 thread 작업이 완료되면
		// 다른 thread가 작업을 처리하기 위해 임계 영역에 진입할 수 있도록
		// event 객체를 signaled 상태로 변경
		// 자동 리셋 모드라 다른 thread 하나가 진입하고
		// non-signaled 상태로 곧바로 변경
		SetEvent(hEvent);
	}

	return 0;
}

int main()
{
	// 자동 리셋 모드는
	// signaled 상태가 되면 바로 non-signaled 상태로 변경된다.
	// (마치 방아쇠(trigger)를 당겼다가 놓으면 원래 위치로 돌아오듯이)
	// 그리고 signaled 상태가 되면,
	// 오직 한개의 thread만 다음 코드를 실행할 수 있음.
	// 나머지는 계속 대기

	// 수동 리셋 모드는
	// signaled 상태가 되면 계속 signaled 상태를 유지한다.
	// 이 때는 한개의 thread가 아니라 여러 thread가 모두 다음 코드를 실행할 수 있음
	// 명시적으로 ResetEvent()를 호출해서 non-signaled 상태로 변경 가능

	// 자동 리셋 모드, non-signaled 상태 event 객체 생성
	hEvent = CreateEvent(
		nullptr,
		false, // 2번째 인자가 수동/자동 리셋 모드를 결정한다.
		false, // signaled? non-signaled?
		nullptr);

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

	// signaled 상태로 세팅
	// 자동 리셋 모드이기 때문에
	// 하나의 thread만 코드 실행이 가능하고
	// 바로 non-signaled 상태로 변경
	SetEvent(hEvent);

	// thread가 작업을 다 마치고
	// return할 때까지 기다림
	WaitForSingleObject(hThread1, INFINITE);
	WaitForSingleObject(hThread2, INFINITE);

	CloseHandle(hThread1);
	CloseHandle(hThread2);
	CloseHandle(hEvent);

	cout << "gNumber: " << gNumber << endl;
}