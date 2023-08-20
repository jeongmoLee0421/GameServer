#include <new> // bad_alloc

#include "RingBuffer.h"
#include "Monitor.h"

RingBuffer::RingBuffer()
	: mBeginMark{ nullptr }
	, mEndMark{ nullptr }
	, mCurrentMark{ nullptr }
	, mGetBufferMark{ nullptr }
	, mLastMoveMark{ nullptr }
	, mBufferSize{ 0 }
	, mUsedBufferSize{ 0 }
	, mTotalUsedBufferSize{ 0 }
	, mSyncObject{}
{
}

RingBuffer::~RingBuffer()
{
	delete[] mBeginMark;
}

bool RingBuffer::Create(int bufferSize)
{
	try
	{
		mBeginMark = new char[bufferSize];
	}
	catch (const std::bad_alloc& exception)
	{
		const char* errorStr = exception.what();

		// 동적 할당 실패
		return false;
	}

	mBufferSize = bufferSize;

	// 버퍼 마지막 한칸을 비워두는 이유를 정확히 모르겠음
	// 원형 큐에 들어온 데이터의 수를 체크하지 않을 때
	// front와 rear가 같은 위치에 있는 경우
	// 가득 찬건지, 비어있는건지 구분이 불가능하기 때문에
	// 한 칸을 비워놓고 구분하는 것으로 알고 있다.

	// 그런데 지금 구현하고 있는 ring buffer는
	// 데이터가 가득 찼는지 여부보다는
	// 배열의 끝으로 가면,
	// 다시 앞으로 이동해서 그대로 덮어쓴다.
	// 이런 경우에 굳이 한칸을 비워둘 필요가 없다고 생각한다.
	//mEndMark = mBeginMark + mBufferSize - 1;
	mEndMark = mBeginMark + mBufferSize;

	Initialize();

	return true;
}

bool RingBuffer::Initialize()
{
	// 데이터를 수신하기 위해 또는 송신하기 위해 포인터 변수를 수정하는데
	// 여러 thread에서 병렬적으로 접근하면, 데이터가 안전하게 변경되지 않는다.
	// 때문에 버퍼에 관련된 정보를 수정할 때는
	// 오직 하나의 thread만이 접근 가능하도록 lock을 걸자.
	//
	// Owner 객체를 생성하면,
	// EnterCriticalSection()이 호출되어 lock이 걸린다.
	// 함수 호출이 종료되어,
	// 스택 메모리가 해제되어 소멸자가 호출되면,
	// LeaveCriticalSection()이 호출되어 lock이 해제된다.
	Monitor::Owner lock{ mSyncObject };

	mCurrentMark = mBeginMark;
	mGetBufferMark = mBeginMark;
	mLastMoveMark = mEndMark;

	mUsedBufferSize = 0;
	mTotalUsedBufferSize = 0;

	return true;
}

char* RingBuffer::MoveMark(int moveLength)
{
	char* pPrevCurrentMark{ nullptr };

	Monitor::Owner lock{ mSyncObject };

	// 송신할 데이터를 ring buffer에 쓰기 위해
	// 추가적인 공간을 마련하려고 했는데
	// 총 사용공간 + 앞으로 사용할 공간이 ring buffer 크기보다 크다면,
	// 더 이상 데이터를 받을 수 없는 상태이다.
	// 만약 받게되면,
	// 송신되지 않는 부분의 데이터 위에 덮어 쓰인다.
	if (mUsedBufferSize + moveLength > mBufferSize)
	{
		return nullptr;
	}

	// EndMark와 CurrentMark 사이에 충분한 공간이 있어서
	// 추가적인 작업 없이 바로 공간을 마련할 수 있다.
	if (mEndMark - mCurrentMark >= moveLength)
	{
		pPrevCurrentMark = mCurrentMark;
		mCurrentMark += moveLength;
	}
	// 이번에는 충분한 공간이 없어서
	// 버퍼의 앞으로 이동해서 공간을 마련해준다.
	// 앞쪽 데이터는 오래 전에 처리되었기 때문에
	// 덮어 씌워도 문제가 없다.
	else
	{
		// 배열의 앞 쪽으로 포인터를 옮기기 전에
		// 데이터를 어디까지 썼는지 위치를 기록
		mLastMoveMark = mCurrentMark;

		pPrevCurrentMark = mBeginMark;
		mCurrentMark = mBeginMark + moveLength;
	}

	mUsedBufferSize += moveLength;
	mTotalUsedBufferSize += moveLength;

	// 송신할 데이터를 위한 공간은 이 위치부터 시작
	return pPrevCurrentMark;
}

char* RingBuffer::MoveMark(int moveLength, int maxRecvLength, DWORD numOfBytesRecv)
{
	Monitor::Owner lock{ mSyncObject };

	// 현재 사용중인 버퍼 크기가 있고
	// moveLength 만큼 움직여야 하고
	// (패킷을 수신하는 첫 과정이라면, 양수가 되어서 공간을 마련해줘야 하고
	// 패킷을 일부만 받은 과정이라면, 음수가 되어 공간을 다시 줄여준다.)
	if (mUsedBufferSize + moveLength + maxRecvLength > mBufferSize)
	{
		return nullptr;
	}

	// 버퍼의 끝 위치를 가리키는 EndMark에서
	// 지금까지 마련해준 공간의 마지막 공간의 다음 공간을 가리키고 있는 CurrentMark를 빼준 값이
	// 앞으로 수신할 버퍼의 크기(moveLength + maxRecvLength)보다 크면
	// CurrentMark 뒷공간에 할당해주면 된다.
	if (mEndMark - mCurrentMark >= // 포인터 사이의 거리를 구하는 연산 결과가 long long이라서
		static_cast<long long>(moveLength) + maxRecvLength) // int 사이의 합 연산에서 오버플로우 방지
	{
		mCurrentMark += moveLength;
	}
	// 그런데 CurrentMark 뒷공간이 충분하지 않다면,
	else
	{
		// 버퍼의 앞으로 가기전에
		// 마지막까지 사용한 위치가 어디인지 저장
		mLastMoveMark = mCurrentMark;

		// 뒤쪽 공간이 부족하기 때문에
		// 일부만 받은 패킷을 BeginMark위치로 복사해서
		// 앞쪽 공간에서 받도록 함.
		// (앞쪽 공간은 이미 처리되었기 때문에 덮어 써도 문제가 없다.)
		CopyMemory(mBeginMark,
			// 만약에 총 패킷의 길이가 6바이트인데 내가 4바이트만 수신한 상태라면,
			// moveLength = -2
			// (현재까지 수신한 바이트 - (할당 받은 버퍼의 끝 위치(CurrentMark) - 할당 받은 버퍼의 시작 위치(pBuf)))
			// numOfBytesRecv = 4가 되어서
			// mCurrentMark에서 6바이트 이동한 부분부터
			// 현재까지 수신한 패킷 길이(numOfBytesRecv)만큼 복사하면 된다.
			mCurrentMark - (numOfBytesRecv - moveLength),
			numOfBytesRecv);

		mCurrentMark = mBeginMark + numOfBytesRecv;
	}

	mUsedBufferSize += moveLength;
	mTotalUsedBufferSize += moveLength;

	// 수신할 데이터를 위한 공간은 이 위치부터 시작
	return mCurrentMark;
}

void RingBuffer::ReleaseBuffer(int releaseSize)
{
	Monitor::Owner lock{ mSyncObject };

	mUsedBufferSize -= releaseSize;
}

char* RingBuffer::GetBuffer(int requestSendSize, int* realSendSize)
{
	// 데이터를 송신하기 위한 메모리 시작 주소
	char* pSendStartPosition{ nullptr };

	Monitor::Owner lock{ mSyncObject };

	// GetBufferMark가 의미하는 것이 어디까지 송신이 완료되었나? 인데
	// 마지막 위치까지 송신이 완료되었으니
	// 버퍼의 앞으로 이동해서 송신 가능한 공간을 지정해줘야함
	if (mLastMoveMark == mGetBufferMark)
	{
		mLastMoveMark = mEndMark;
		mGetBufferMark = mBeginMark;
	}

	// 송신하기 위해 준비중인 데이터의 양이
	// 최대 송신 요청량보다 많다면
	if (mUsedBufferSize > requestSendSize)
	{
		// mLastMoveMark: 송신할 데이터가 존재하는 마지막 위치의 다음 위치
		// mGetBufferMark: 현재까지 전송한 데이터 위치의 다음 위치
		// requestSendSize: 최대로 전송 가능한 바이트 길이
		
		// LastMoveMark와 GetBufferMark 사이에 충분한 데이터 양이 있다면
		if (mLastMoveMark - mGetBufferMark >= requestSendSize)
		{
			*realSendSize = requestSendSize;
			
			pSendStartPosition = mGetBufferMark;

			mGetBufferMark += requestSendSize;
		}
		// 사이에 데이터 양이 충분하지 않다면
		else
		{
			// 버퍼의 끝 위치까지만 보낸다.
			*realSendSize = static_cast<int>(mLastMoveMark - mGetBufferMark);

			pSendStartPosition = mGetBufferMark;

			mGetBufferMark += *realSendSize;
		}
	}
	// 송신하기 위해 준비중인 데이터의 양이
	// 최대 송신 요청량보다 적지만,
	// 보낼 데이터가 있다면
	else if(mUsedBufferSize > 0)
	{
		// LastMoveMark와 GetBufferMark 사이에 송신할 데이터가 모두 존재하면
		if (mLastMoveMark - mGetBufferMark >= mUsedBufferSize)
		{
			*realSendSize = mUsedBufferSize;

			pSendStartPosition = mGetBufferMark;

			mGetBufferMark += mUsedBufferSize;
		}
		// 사이에 데이터가 모두 존재하지 않으면,
		else
		{
			*realSendSize = static_cast<int>(mLastMoveMark - mGetBufferMark);

			pSendStartPosition = mGetBufferMark;

			mGetBufferMark += *realSendSize;
		}
	}

	return pSendStartPosition;
}

int RingBuffer::GetBufferSize()
{
	return mBufferSize;
}

int RingBuffer::GetUsedBufferSize()
{
	return mUsedBufferSize;
}

int RingBuffer::GetTotalUsedBufferSize()
{
	return mTotalUsedBufferSize;
}

char* RingBuffer::GetBeginMark()
{
	return mBeginMark;
}

char* RingBuffer::GetCurrentMark()
{
	return mCurrentMark;
}

char* RingBuffer::GetEndMark()
{
	return mEndMark;
}
