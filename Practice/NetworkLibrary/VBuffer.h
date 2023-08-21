#pragma once

// 2023 08 20 이정모 home

// 가변 길이 패킷을 처리하기위한 class

// 고정 길이 패킷을 사용한다면,
// 내가 송수신할 패킷의 크기가 명확하게 정해져있다는 뜻이고
// 이는 구조체로써 표현이 가능하다.
// 그래서 패킷의 크기만큼 수신하게 되면,
// 구조체의 멤버 변수를 통해 데이터를 접근하기에 좋다.
// 다만, 보낼 데이터가 적은 경우에도
// 고정된 패킷의 크기만큼 전송해야 하기 떄문에
// 불필요한 데이터 전송이 발생한다.

// 가변 길이 패킷을 사용한다면,
// 송신할 데이터의 양이 작으면, 패킷의 길이도 작아지고
// 송신할 데이터의 양이 많으면, 패킷의 길이도 커진다.
// 즉, 패킷 길이에 맞추어서 데이터를 전송하기 때문에
// 효율적인 데이터 전송이 발생한다.
// 다만, 패킷의 길이가 가변이라서
// 패킷을 구조체로 정형화할 수 없기 때문에
// 송신하는 쪽에서 데이터를 입력했을 때
// 수신하는 쪽에서는 송신하는 쪽에서 입력한 데이터 순서대로 읽어야 하기 때문에
// 처리가 조금 더 복잡하다.

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif

#include "Singleton.h"

// Singleton class를 상속해서
// 전역 위치 어디에서든 편하게 가변 길이 패킷을 처리하도록 했다.
// 패킷 처리가 끝날 때까지
// 데이터 버퍼를 가리키는 포인터가 변경되면 문제가 발생할 수 있는점 유의하자.
// 당연히 패킷 처리하면서 데이터를 뽑아올 때는 lock을 걸어야 한다.
class NETLIB_API VBuffer : public Singleton
{
	DECLEAR_SINGLETON(VBuffer);

public:
	// 외부에서 수신한 가변 패킷 데이터가 있을텐데
	// 자료형만큼 데이터를 읽고
	// 자료형만큼 버퍼 포인터를 미는 함수
	void GetChar(char& ch);
	void GetShort(short& num);
	void GetInteger(int& num);
	void GetStream(char* pBuffer, short length);

	// 문자열을 읽는 함수로
	// 선두 2바이트에 문자열의 길이 정보를 읽어야 한다.
	void GetString(char* pBuffer);

public:
	// 내부 가변 버퍼에
	// 필요한 데이터를 세팅하고
	// 자료형만큼 버퍼 포인터를 미는 함수.
	// 데이터 세팅이 완료되면, CopyBuffer()를 호출하여
	// 데이터 송신을 위한 버퍼에 복사한다.
	void SetChar(char ch);
	void SetShort(short num);
	void SetInteger(int num);
	void SetStream(char* pBuffer, short length);

	// 문자열을 세팅하는 함수로
	// 선두 2바이트에 문자열의 길이 정보를 넣어야 한다.
	void SetString(char* pBuffer);

public:
	// 데이터를 추출하기 위해
	// 외부에서 수신한 가변 패킷 데이터가 담긴 버퍼의
	// 시작 위치를 세팅
	void SetBuffer(char* pVBuffer);

	// 내부 가변 버퍼에
	// 가변 데이터를 세팅하기 전에
	// 필요한 작업
	void PrepareForDataSetting();

	// 송신할 데이터를
	// 내부 가변 버퍼에 모두 세팅했으면,
	// 실제 송신할 버퍼에 복사하는 함수
	bool CopyBuffer(char* pDstBuffer);

public:
	int GetMaxBufSize();
	int GetCurrentBufSize();
	char* GetCurrentMark();
	char* GetBeginMark();

private:
	// 데이터를 세팅할 실제 내부 버퍼 시작 포인터
	char* mVBuffer;
	
	// 데이터를 추출할 때는 외부 버퍼의 위치를 가리키고
	// 데이터를 세팅할 때는 내부 버퍼의 위치를 가리키는 포인터
	char* mCurrentMark;

	// 내부 버퍼 최대 크기
	int mMaxBufSize;

	// 내부 버퍼에 세팅한 데이터를
	// 외부에 복사해야 할 때
	// 얼마나 복사해야 하는지
	// 버퍼 크기를 의미
	int mCurrentBufSize;

	VBuffer(const VBuffer& rhs) = delete;
	VBuffer(VBuffer&& rhs) = delete;

	VBuffer& operator=(const VBuffer& rhs) = delete;
	VBuffer& operator=(VBuffer&& rhs) = delete;
};