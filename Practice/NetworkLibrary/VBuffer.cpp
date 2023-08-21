#define _WINSOCKAPI_
#include <Windows.h>

#include "VBuffer.h"
#include "Singleton.h"

constexpr int MAX_VBUFFER_SIZE = 1024 * 50;
constexpr int MAX_PBUFSIZE = 4096; // PacketPool에서 버퍼 한개당 size라는데 아직은 잘 모름
constexpr int PACKET_SIZE_LENGTH = 4;

IMPLEMENT_SINGLETON(VBuffer);

void VBuffer::Initialize()
{
	mVBuffer = new char[MAX_VBUFFER_SIZE] {};
	mMaxBufSize = MAX_VBUFFER_SIZE;

	PrepareForDataSetting();
}

void VBuffer::Finalize()
{
	delete[] mVBuffer;
}

void VBuffer::GetChar(char& ch)
{
	ch = *mCurrentMark;

	mCurrentMark += 1;
}

void VBuffer::GetShort(short& num)
{
	// 가장 아래 바이트를 읽어오고
	num = *mCurrentMark +
		// 그 다음 바이트를 읽어오는데
		// 8비트를 밀어서 원래 위치(두번째 바이트)를 복구
		(*(mCurrentMark + 1) << 8);

	mCurrentMark += 2;
}

void VBuffer::GetInteger(int& num)
{
	num = *mCurrentMark +
		(*(mCurrentMark + 1) << 8) +
		(*(mCurrentMark + 2) << 16) +
		(*(mCurrentMark + 3) << 24);

	mCurrentMark += 4;
}

void VBuffer::GetStream(char* pBuffer, short length)
{
	if (0 > length || MAX_PBUFSIZE < length)
	{
		return;
	}

	// 문자열이 아니고
	// 바이트 스트림이기 때문에
	// 바이트 단위로 복사
	CopyMemory(pBuffer, mCurrentMark, length);

	mCurrentMark += length;
}

void VBuffer::GetString(char* pBuffer)
{
	short length{ 0 };
	GetShort(length);

	if (0 > length || MAX_PBUFSIZE < length)
	{
		return;
	}

	CopyMemory(pBuffer, mCurrentMark, length);

	// 문자열을 추출을 CopyMemory(memcpy)로 수행했기 때문에
	// 뒤에 널문자를 붙여주자
	*(pBuffer + length) = '\0';

	mCurrentMark += length;
}

void VBuffer::SetChar(char ch)
{
	*mCurrentMark = ch;

	mCurrentMark += 1;

	// 내가 몇 바이트를 세팅했는지 누적해서 계산해두고
	// 데이터 세팅이 완료되면,
	// 외부 송신 버퍼에 복사할 때
	// 세팅한 총 바이트만큼 복사하기 위함
	mCurrentBufSize += 1;
}

void VBuffer::SetShort(short num)
{
	// 가장 아래 바이트 세팅
	*mCurrentMark = static_cast<char>(num);

	// 왼쪽으로 8비트 밀어서
	// 두번째 바이트 세팅
	*(mCurrentMark + 1) = num >> 8;

	mCurrentMark += 2;
	mCurrentBufSize += 2;
}

void VBuffer::SetInteger(int num)
{
	*mCurrentMark = static_cast<char>(num);
	*(mCurrentMark + 1) = num >> 8;
	*(mCurrentMark + 2) = num >> 16;
	*(mCurrentMark + 3) = num >> 24;

	mCurrentMark += 4;
	mCurrentBufSize += 4;
}

void VBuffer::SetStream(char* pBuffer, short length)
{
	CopyMemory(mCurrentMark, pBuffer, length);

	mCurrentMark += length;
	mCurrentBufSize += length;
}

void VBuffer::SetString(char* pBuffer)
{
	short length = static_cast<short>(strlen(pBuffer));
	
	if (0 > length || MAX_PBUFSIZE < length)
	{
		return;
	}
	
	// 문자열을 세팅할 때는
	// 선두 2바이트에 길이 정보를 넣는다.
	SetShort(length);

	CopyMemory(mCurrentMark, pBuffer, length);

	mCurrentMark += length;
	mCurrentBufSize += length;
}

void VBuffer::SetBuffer(char* pVBuffer)
{
	mCurrentMark = pVBuffer;
}

void VBuffer::PrepareForDataSetting()
{
	// 선두 4바이트는 패킷의 길이를 나타냄
	mCurrentMark = mVBuffer + PACKET_SIZE_LENGTH;

	// 패킷의 길이도 데이터를 세팅하는 것이기 때문에
	// 현재 세팅된 버퍼 크기도 4바이트로 시작
	mCurrentBufSize = PACKET_SIZE_LENGTH;
}

bool VBuffer::CopyBuffer(char* pDstBuffer)
{
	// 선두 4바이트에는 패킷의 전체 길이 정보를 저장
	CopyMemory(mVBuffer, &mCurrentBufSize, PACKET_SIZE_LENGTH);

	// 송신할 데이터 시작 위치부터
	// 전체 길이 만큼 복사
	CopyMemory(pDstBuffer, mVBuffer, mCurrentBufSize);

	return true;
}
