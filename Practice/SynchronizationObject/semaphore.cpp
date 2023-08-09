// 2023 08 09 이정모 home

// 커널 모드 동기화 객체인 semaphore
// semaphore 내부 변수 max count가 2 이상이라면
// 여러 thread가 공유 자원에 동시에 접근할 수도 있다.
// 이 경우에는 mutex와 같은 다른 동기화 오브젝트와 같이 사용해서 작업을 처리해야 한다.
// max count가 1이라면 이는 binary semaphore로 mutex와 동일한 기능을 수행한다.

#include <Windows.h>
#include <iostream>
#include <process.h>
using namespace std;

int gNumber{ 0 };
HANDLE hSemaphore{ nullptr };

unsigned int __stdcall ThreadFunc(void*)
{
	long prevCount{ 0 };

	for (int i = 0; i < 100000; ++i)
	{
		// semaphore의 count가 0이면 계속 대기하고 있다가
		// 1 이상이 되면 thread가 진입 가능
		WaitForSingleObject(hSemaphore, INFINITE);

		gNumber++;
		//cout << gNumber << endl;

		// 한 thread가 작업을 마치고 나가기 때문에
		// 다른 thread가 임계 영역에 진입할 수 있도록
		// semaphore count를 1 증가
		ReleaseSemaphore(hSemaphore, 1, &prevCount);

		// semaphore는 ReleaseSemaphore()를 반드시 호출해야함
		// signaled 여부를 count가 1이상인지로 계산하기 때문에
		// thread가 종료될 때 count가 증가하지 않기 때문에
		// semaphore가 signaled 상태로 변경되지 않는다.
	}

	return 0;
}

int main()
{
	hSemaphore = CreateSemaphore(
		nullptr,
		0, // 초기 count값을 0으로 주어 진입 방지
		1, // max count를 1로 하여 mutex처럼 동작
		nullptr
	);

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

	// semaphore count를 1증가시킴으로써
	// 하나의 thread가 임계영역에 진입 가능하도록 함
	// semaphore 역시 mutex처럼 자동 리셋 모드이다.
	long prevCount{ 0 };
	ReleaseSemaphore(hSemaphore, 1, &prevCount);

	// thread가 작업을 다 마치고
	// return할 때까지 기다림
	WaitForSingleObject(hThread1, INFINITE);
	WaitForSingleObject(hThread2, INFINITE);

	CloseHandle(hThread1);
	CloseHandle(hThread2);
	CloseHandle(hSemaphore);

	cout << "gNumber: " << gNumber << endl;
}