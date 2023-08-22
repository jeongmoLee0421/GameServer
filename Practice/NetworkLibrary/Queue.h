#pragma once

// 2023 08 22 이정모 home

// 먼저 들어간 데이터를
// 먼저 꺼내는 queue.
// monitor class를 내부적으로 포함해서
// 여러 thread가 동시에 접근하는 것을 막음

// 지금은 함수 내부에서 lock을 걸고 있지만,
// 실제로 queue를 사용할 때는
// 여러 함수의 조합으로 결과를 만들어낸다(IsEmpty() + Front() + Pop()).
// 함수의 조합이 전부 수행되기 전에 thread 사이에 경합이 발생할 수 있기 때문에
// 합수의 조합 시작에 lock을 걸고
// 함수가 결과를 만들어 내면 lock을 해제하는 방식으로
// 가야한다고 생각한다.

//#define NDEBUG
#include <cassert>

#include "Monitor.h"

// 상수를 헤더파일에 선언하고 여러 곳에서 include하더라도
// 중복 정의 에러가 발생하지 않는다.
// 왜냐하면, 컴파일러가 상수를 읽는 순간
// 변수처럼 메모리를 잡는 것이 아니라
// 해당 심볼(상수 이름)과 동일한 심볼을 만나면,
// 단순히 수로 치환해서 컴파일하기 때문이다.
constexpr int MAX_QUEUESIZE{ 10000 };

template <typename T>
class Queue
{
public:
	Queue<T>(int maxSize = MAX_QUEUESIZE);
	~Queue<T>();

public:
	bool Push(T value);
	bool Pop();

	bool IsEmpty();

public:
	// 가장 앞에 있는 데이터를 반환해주는 것임
	// 데이터를 삭제하는건 pop()
	T Front();

	int GetCurrentSize();
	int GetMaxSize();
	void Clear();

private:
	T* mArr;
	Monitor mSyncObject;

private:
	int mMaxSize;
	int mCurrentSize;

private:
	int mFront;
	int mRear;
};

template<typename T>
inline Queue<T>::Queue(int maxSize)
	: mSyncObject{}
	, mMaxSize{ maxSize }
	, mCurrentSize{ 0 }
	, mFront{ 0 }
	, mRear{ 0 }
{
	mArr = new T[mMaxSize]{};
}

template<typename T>
inline Queue<T>::~Queue()
{
	delete[] mArr;
}

template<typename T>
inline bool Queue<T>::Push(T value)
{
	// queue에 데이터를 추가할 때
	// 다른 thread에서 동시에 queue의 멤버 변수에 접근하면,
	// 문제가 발생하니 lock을 걸자
	Monitor::Owner lock{ mSyncObject };

	// 데이터를 추가하기 위해 lock을 걸었는데
	// IsFull() 함수에서 데이터가 가득 차있는지 확인하기 위해
	// lock을 또 걸려고 하면,
	// 이미 lock이 걸려있어서 문제가 발생할 여지가 있다.
	/*if (IsFull())
	{
		return false;
	}*/

	if (mMaxSize == mCurrentSize)
	{
		return false;
	}

	// 데이터를 넣다보니 배열의 끝에 도달한 경우
	// 다시 앞쪽으로 돌아가서 데이터를 넣기 위함
	// 원형 큐와 비슷함
	// rear가 front를 침범할 가능성이 있지 않는가?
	// 라고 생각할 수 있지만,
	// 위에서 데이터가 가득 차있는지 확인하기 때문에
	// 문제가 없다.
	if (mMaxSize == mRear)
	{
		mRear = 0;
	}

	mArr[mRear] = value;
	++mRear;

	++mCurrentSize;

	return true;
}

template<typename T>
inline bool Queue<T>::Pop()
{
	Monitor::Owner lock{ mSyncObject };

	// 역시 pop()에서 lock을 걸고
	// IsEmpty()에서 lock을 걸면,
	// 이중으로 lock을 걸어 문제가 발생할 여지가 있기 때문에
	/*if (IsEmpty())
	{
		return false;
	}*/

	// 함수를 호출하지 않고
	// 직접 확인한다.
	if (0 == mCurrentSize)
	{
		return false;
	}

	// pop()이 가장 앞쪽의 데이터를 삭제하는 함수인데
	// index를 나타내는 front를 하나 올려주면,
	// 뒤쪽 데이터는 접근하지 못하기 때문에
	// 삭제라고 생각할 수 있음
	++mFront;

	--mCurrentSize;

	return true;
}

template<typename T>
inline bool Queue<T>::IsEmpty()
{
	Monitor::Owner lock{ mSyncObject };

	return 0 == mCurrentSize;
}

template<typename T>
inline T Queue<T>::Front()
{
	Monitor::Owner lock{ mSyncObject };

	// 큐가 비었는데
	// 데이터를 읽으려고 하는 것은
	// 에러로 처리
	if (0 == mCurrentSize)
	{
		assert(nullptr && "queue is empty");
	}

	// 큐에 데이터가 있지만,
	// front가 배열의 맨 끝에 위치한다면,
	// 앞으로 복귀시켜서 데이터를 반환해준다.
	if (mMaxSize == mFront)
	{
		mFront = 0;
	}

	return mArr[mFront];
}

template<typename T>
inline int Queue<T>::GetCurrentSize()
{
	Monitor::Owner lock{ mSyncObject };

	return mCurrentSize;
}

template<typename T>
inline int Queue<T>::GetMaxSize()
{
	// 큐의 최대크기는
	// 실시간으로 변하는 것이 아니고 고정이기 때문에
	// lock을 걸 필요가 없다.
	return mMaxSize;
}

template<typename T>
inline void Queue<T>::Clear()
{
	Monitor::Owner lock{ mSyncObject };

	mFront = 0;
	mRear = 0;
	mCurrentSize = 0;
}
