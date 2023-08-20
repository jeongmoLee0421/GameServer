#pragma once

// 2023 08 19 이정모 home

// 송신 및 수신할 데이터를 모아두는 buffer
// (물리적으로는 선형이지만, 논리적으로는 끝과 시작이 연결되어 있는 Ring)
// 
// 하나의 패킷을 송신할 때마다 send()를 호출하고
// 하나의 패킷을 수신할 때마다 recv()를 호출하면,
// 너무 많은 유저 - 커널 모드 전환이 발생한다.
// 
// 유저 프로세스 정보를 저장하고 커널 정보를 로드하고
// 커널 작업을 완료하면,
// 커널 정보를 저장하고 유저 프로세스 정보를 로드하는
// 문맥 교환이 자주 발생하면,
// 그만큼 CPU 처리량도 적어진다.
//
// 그래서 임의의 버퍼를 하나 잡아두고서
// 송신할 데이터를 ring buffer에 모아두었다가 한번에 최대한 많이 송신하고
// 수신할 데이터를 ring buffer에 한번에 최대한 많이 수신하여
// send(), recv() 함수 호출을 최소한으로 한다.
//
// 버퍼가 원형으로 이어져있기 때문에
// 이미 사용한 공간 위에 새로운 데이터를 덮어 씌움으로써
// 과거 데이터에 대한 처리를 신경 쓰지 않아도 된다.

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif

#define _WINSOCKAPI_
#include <Windows.h>

#include "Monitor.h"

constexpr int MAX_RINGBUFSIZE{ 1024 * 100 };

class NETLIB_API RingBuffer
{
public:
	RingBuffer();
	~RingBuffer();

public:
	// 링 버퍼 메모리 동적 할당
	bool Create(int bufferSize = MAX_RINGBUFSIZE);
	bool Initialize();

public:
	// 송신할 데이터를 저장하기 위한 공간 마련.
	// 마련된 공간만큼 currentMark가 이동
	char* MoveMark(int moveLength);

	// 수신할 데이터를 저장하기 위한 공간을 마련하는 것으로
	// 수신 같은 경우는 TCP 특성 상
	// 한 번에 다 수신하지 못하면, 여러번에 걸쳐서 수신을 받아야 하는데
	// 이 처리를 해주는 함수
	// moveLength: 패킷 중 일부만 수신했다면, 뒤로 움직여야함
	// maxRecvLength: 앞으로 최대로 받을 수 있는 바이트 길이
	// numOfBytesRecv: 현재까지 받은 패킷의 길이
	char* MoveMark(int moveLength, int maxRecvLength, DWORD numOfBytesRecv);

	// 데이터 송수신을 위해 버퍼의 특정 부분에 대한 공간을 마련받았고
	// 해당 공간에 데이터를 받아서 작업을 완료했다면,
	// 총 사용중인 바이트에서 완료된 송수신에 사용된 바이트를 빼준다.
	// 사용이 완료되었으니 해당 공간은 다른 데이터로 덮어 써도 되기 떄문
	void ReleaseBuffer(int releaseSize);

public:
	// 송신할 데이터를 버퍼에 넣었고
	// WSASend()를 호출해서 데이터를 송신할건데
	// 최대 requestSendSize만큼 송신이 가능하고
	// 실제로 송신 가능한 크기는 realSendSize에 넣어준다.
	char* GetBuffer(int requestSendSize, int* realSendSize);

public:
	// ring buffer 크기
	int GetBufferSize();

	// 현재 사용중인 버퍼의 크기
	int GetUsedBufferSize();

	// 송신 및 수신을 위해 할당해준,
	// 총 사용된 버퍼 크기
	int GetTotalUsedBufferSize();

	char* GetBeginMark();
	char* GetCurrentMark();
	char* GetEndMark();

public:
	// client와 데이터를 송수신하기 위한 버퍼로
	// 하나를 만들어두면,
	// 추가적인 생성 및 복사를 할 이유가 없다.
	// 실수를 방지하기 위해 delete
	RingBuffer(const RingBuffer& rhs) = delete;
	RingBuffer(RingBuffer&& rhs) noexcept = delete;

	RingBuffer& operator=(const RingBuffer& rhs) = delete;
	RingBuffer& operator=(RingBuffer&& rhs) = delete;

private:
	// 버퍼의 시작 위치
	char* mBeginMark;

	// 버퍼의 끝 위치
	char* mEndMark;

	// 어느 위치까지 내가 공간을 마련했는지(또는 할당해주었는지)?
	char* mCurrentMark;

	// 데이터 송신이 어느 위치까지 진행되었는지?
	char* mGetBufferMark;

	// mCurrentMark 위치부터 마지막 위치까지 공간이 충분하지 않다면,
	// 뒷 공간을 사용하지 않고 맨 앞으로 이동하여 버퍼 포인터를 세팅하는 방식으로 동작한다.
	// 이 때 유효한 데이터가 있는 마지막 위치를 저장하는 포인터
	// 즉, ring buffer를 recycle하기 전에
	// 어느 위치까지 사용했나?
	char* mLastMoveMark;

	// 총 버퍼 크기
	int mBufferSize;

	// 현재 사용중인 버퍼 크기
	int mUsedBufferSize;

	// 모든 송수신에 사용된 버퍼 크기
	int mTotalUsedBufferSize;

	// 버퍼 공간을 할당하기 위해
	// 포인터들을 변경할건데
	// send(), recv() 처리가
	// thread를 통해 병렬적으로 이루어지기 때문에
	// lock을 걸어야한다.
	Monitor mSyncObject;
};