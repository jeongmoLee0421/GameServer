// 2023 08 09 이정모 home

// 커널 모드 동기화 객체인 mutex
// 내부가 유저 모드 동기화 객체인 critical_section으로 구현된 std::mutex와 다름

#include <Windows.h>
#include <iostream>
#include <process.h>
using namespace std;

int gNumber{ 0 };
HANDLE hMutex{ nullptr };

unsigned int __stdcall ThreadFunc(void* arg)
{
	int threadNumber = *((int*)arg);

	for (int i = 0; i < 3; ++i)
	{
		// 대기하고 있다가
		// signaled 상태가 되면 진입
		// mutex는 자동 리셋 모드이기 때문에
		// 하나의 thread만 진입하고 바로 non-signaled 상태로 변경
		WaitForSingleObject(hMutex, INFINITE);

		cout << threadNumber << endl;
		gNumber++;

		// signaled 상태로 변경
		//ReleaseMutex(hMutex);
		//ReleaseMutex(hMutex);

		// ReleaseMutex()를 호출한 경우
		// 여러 thread들이 번갈아가며 작업을 수행하는데
		// 
		// ReleaseMutex()를 호출하지 않은 경우
		// 1번 스레드가 1000번 루프 돌고 return 하고
		// 3번 스레드가 1000번 루프 돌고 return 하고
		// 2번 스레드가 마지막으로 1000번 루프 돌고 return한다.
		//
		// ReleaseMutex()를 호출하지 않게되면
		// mutex를 소유한 thread 본인의 작업이 다 완료될 때까지 mutex를 소유하고 있다가
		// 작업이 끝나고 return하면 thread가 signaled 상태로 변경되는데 이때 소유하고 있던 mutex를 signaled 상태로 변경하는 것 같다.
		// 이렇게 해야 한 thread의 작업이 완료되면 다음 thread가 진입할 수 있게 된다.
	}

	return 0;
}

int main()
{
	hMutex = CreateMutex(nullptr,
		true, // true라면 mutex를 만든 thread가 소유권을 얻음(non-signaled)
		nullptr);

	unsigned int threadID{};

	int threadNumber1 = 1;
	HANDLE hThread1 = (HANDLE)_beginthreadex(
		nullptr,
		0,
		ThreadFunc,
		(void*)&threadNumber1,
		CREATE_SUSPENDED, // thread를 생성하면서 곧바로 함수를 수행하지 않고 이후에 원하는 타이밍에 수행 가능
		&threadID
	);

	cout << "create thread id: " << threadID << endl;
	if (nullptr == hThread1)
	{
		cout << "thread 1 is nullptr" << endl;
	}

	int threadNumber2 = 2;
	HANDLE hThread2 = (HANDLE)_beginthreadex(
		nullptr,
		0,
		ThreadFunc,
		(void*)&threadNumber2,
		CREATE_SUSPENDED,
		&threadID
	);

	cout << "create thread id: " << threadID << endl;
	if (nullptr == hThread2)
	{
		cout << "thread 2 is nullptr" << endl;
	}

	int threadNumber3 = 3;
	HANDLE hThread3 = (HANDLE)_beginthreadex(
		nullptr,
		0,
		ThreadFunc,
		(void*)&threadNumber3,
		CREATE_SUSPENDED,
		&threadID
	);

	cout << "create thread id: " << threadID << endl;
	if (nullptr == hThread3)
	{
		cout << "thread 2 is nullptr" << endl;
	}

	// 원하는 타이밍에 thread 진행
	ResumeThread(hThread1);
	ResumeThread(hThread2);
	ResumeThread(hThread3);

	// mutex 커널 오브젝트를 signaled 상태로 변경
	// mutex도 자동 리셋 모드라서
	// 하나의 thread만 코드를 실행하고
	// 바로 non-signaled 상태로 변경
	ReleaseMutex(hMutex);

	// thread가 작업을 다 마치고
	// return할 때까지 기다림
	WaitForSingleObject(hThread1, INFINITE);
	WaitForSingleObject(hThread2, INFINITE);
	WaitForSingleObject(hThread3, INFINITE);

	CloseHandle(hThread1);
	CloseHandle(hThread2);
	CloseHandle(hThread3);
	CloseHandle(hMutex);

	cout << "gNumber: " << gNumber << endl;
}