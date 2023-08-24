#pragma once

// 2023 08 22 이정모 home

// 게임 서버가 원활하게 돌아가고 있는지
// 중간에 문제는 없는지
// 여러 정보들을 남기는 class.
// server가 패킷을 처리하는 중에 Log를 남기게되면,
// DB나 File에 log를 출력할텐데,
// 출력 속도가 느리다 보니 기다리는 시간도 늘어나서 server의 처리량이 감소하게 된다.
// 그래서 log를 직접 출력하는 것이 아니라 queue에 넣기만 하고
// 다른 thread가 일정 시간마다 log를 출력하는 방식으로 동작한다.

#define _WINSOCKAPI_
#include <Windows.h>
#include <WinSock2.h>

#include "Thread.h"
#include "Singleton.h"
#include "Queue.h"
#include "Monitor.h"

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif

constexpr int MAX_FILENAME_LENGTH = 100;
constexpr int MAX_IP_LENGTH = 20;
constexpr int MAX_DSN_NAME = 100;
constexpr int MAX_DSN_ID = 20;
constexpr int MAX_DSN_PW = 20;
constexpr int DEFAULT_TICK = 1000;
constexpr int DEFAULT_UDPPORT = 1555;
constexpr int DEFAULT_TCPPORT = 1556;
constexpr int MAX_OUTPUT_LENGTH = 1024 * 4;
constexpr int MAX_STORAGE_TYPE = 6;
constexpr int MAX_QUEUECOUNT = 10000;
constexpr int MAX_LOGFILE_SIZE = 1024 * 200000; // 200MB
constexpr int WM_DEBUGMSG = WM_USER + 1;

// 로그를 남길 때
// 로그의 등급을 eLogInfoType으로 남기는데
// 이를 문자열과 매칭시켜서 출력하기 위함
static wchar_t LogInfoType_StringTable[][100] =
{
		L"LOG_NONE",
		L"LOG_INFO_LOW",													//0x00000001
		L"LOG_INFO_NORMAL",												//0x00000002
		L"LOG_INFO_LOW , LOG_INFO_NORMAL",								//0x00000003	
		L"LOG_INFO_HIGH",												//0x00000004	
		L"LOG_INFO_LOW , LOG_INFO_HIGH",									//0x00000005
		L"LOG_INFO_NORMAL , LOG_INFO_HIGH",								//0x00000006
		L"LOG_INFO_LOW , LOG_INFO_NORMAL , LOG_INFO_HIGH",				//0x00000007
		L"LOG_INFO_CRITICAL",											//0x00000008
		L"LOG_INFO_LOW , LOG_INFO_CRITICAL",								//0x00000009
		L"LOG_INFO_NORMAL , LOG_INFO_CRITICAL",							//0x0000000A
		L"LOG_INFO_LOW , LOG_INFO_NORMAL , LOG_INFO_CRITICAL",			//0x0000000B
		L"LOG_INFO_HIGH , LOG_INFO_CRITICAL",							//0x0000000C
		L"LOG_INFO_LOW , LOG_INFO_HIGH , LOG_INFO_CRITICAL",				//0x0000000D
		L"LOG_INFO_NORMAL , LOG_INFO_HIGH , LOG_INFO_CRITICAL",			//0x0000000E
		L"LOG_INFO_ALL",													//0x0000000F

		L"LOG_ERROR_LOW",												//0x00000010
		L"LOG_ERROR_NORMAL",												//0x00000020
		L"LOG_ERROR_LOW , LOG_ERROR_NORMAL",								//0x00000030	
		L"LOG_ERROR_HIGH",												//0x00000040	
		L"LOG_ERROR_LOW , LOG_ERROR_HIGH",								//0x00000050
		L"LOG_ERROR_NORMAL , LOG_ERROR_HIGH",							//0x00000060
		L"LOG_ERROR_LOW , LOG_ERROR_NORMAL , LOG_ERROR_HIGH",			//0x00000070
		L"LOG_ERROR_CRITICAL",											//0x00000080
		L"LOG_ERROR_LOW , LOG_ERROR_CRITICAL",							//0x00000090
		L"LOG_ERROR_NORMAL , LOG_ERROR_CRITICAL",						//0x000000A0
		L"LOG_ERROR_LOW , LOG_ERROR_NORMAL , LOG_ERROR_CRITICAL",		//0x000000B0
		L"LOG_ERROR_HIGH , LOG_ERROR_CRITICAL",							//0x000000C0
		L"LOG_ERROR_LOW , LOG_ERROR_HIGH , LOG_ERROR_CRITICAL",			//0x000000D0
		L"LOG_ERROR_NORMAL , LOG_ERROR_HIGH , LOG_ERROR_CRITICAL",		//0x000000F0 -> 0x000000E0
		L"LOG_ERROR_ALL",												//0x00000100 -> 0x000000F0
		L"LOG_ALL"														//0x00000200 -> 0x000000FF	
};

// 로그의 종류: 알림, 에러
// 로그의 중요도: 낮음, 보통, 높음, 치명
// bitwise or 연산을 하기 위해서
// 값이 2의 n승으로 증가하도록 함.
// 비트 위치가 겹치지 않기 때문에
// 특정 비트가 세팅되어 있다는 것이 어떤 고유한 설정임을 말해줌
enum class eLogInfoType
{
	LOG_NONE = 0x00000000,

	LOG_INFO_LOW = 0x00000001,
	LOG_INFO_NORMAL = 0x00000002,
	LOG_INFO_HIGH = 0x00000004,
	LOG_INFO_CRITICAL = 0x00000008,
	LOG_INFO_ALL = 0x0000000F,

	LOG_ERROR_LOW = 0x00000010,
	LOG_ERROR_NORMAL = 0x00000020,
	LOG_ERROR_HIGH = 0x00000040,
	LOG_ERROR_CRITICAL = 0x00000080,
	LOG_ERROR_ALL = 0x000000F0,

	LOG_ALL = 0x000000FF,
};

// 로그를 출력할 매체
enum class eLogStorageType
{
	STORAGE_FILE = 0x00000000,
	STORAGE_DB = 0x00000001,
	STORAGE_WINDOW = 0x00000002, // 지정한 윈도우에 메시지 전송
	STORAGE_OUTPUTWND = 0x00000003, // Debug 출력창
	STORAGE_UDP = 0x00000004,
	STORAGE_TCP = 0x00000005,
};

// 로그를 출력할 파일의 타입
enum class eLogFileType
{
	FILETYPE_NONE = 0x00000000,
	FILETYPE_XML = 0x00000001,
	FILETYPE_TEXT = 0x00000002,
	FILETYPE_ALL = 0x00000003,
};

struct LogConfig
{
	// log를 출력할 매체를 index로 구분하고
	// 해당 매체에 출력하고 싶은 로그의 등급을 OR 연산해서 세팅
	int mLogInfoTypes[MAX_STORAGE_TYPE];

	wchar_t mLogFileName[MAX_FILENAME_LENGTH];

	eLogFileType mLogFileType;

	// tcp 및 udp로 로그 출력
	char mIP[MAX_IP_LENGTH];
	int mUDPPort;
	int mTCPPort;

	// DB로 로그를 출력하기 위한 DSN 정보
	// DB 서버 주소, DB 이름, ID 및 PW, 기타 연결 관련 설정 등
	char mDSNName[MAX_DSN_NAME];
	char mDSNID[MAX_DSN_ID];
	char mDSNPW[MAX_DSN_PW];

	// log를 다른 윈도우에 출력하고 싶을 때가 있을텐데
	// 출력하고 싶은 윈도우의 핸들이다.
	HWND mHwnd;

	// log를 출력하는 주기로
	// mProcessTick마다 OnProcess() 함수 호출
	DWORD mProcessTick;

	// log 파일 사이즈가 mFileMaxSize보다 크면,
	// 새로운 파일 생성
	DWORD mFileMaxSize;

	LogConfig()
	{
		ZeroMemory(this, sizeof(LogConfig));

		mProcessTick = DEFAULT_TICK;
		mUDPPort = DEFAULT_UDPPORT;
		mTCPPort = DEFAULT_TCPPORT;
		mFileMaxSize = 1024 * 50000; // 50MB
	}
};

// 로그의 등급과
// 사용자가 작성한 문자열로 이루어져 있으며,
// 큐에 들어간다.
struct LogMsg
{
	eLogInfoType mLogInfoType;
	wchar_t mOutputString[MAX_OUTPUT_LENGTH];
};

// thread class를 상속해서
// 일정 시간마다 log를 처리하고
// singleton class를 상속해서
// 서버 전역에서 필요할 때 log를 남길 수 있도록
class NETLIB_API Log : public Thread, public Singleton
{
	DECLEAR_SINGLETON(Log);

public:
	// 설정 값을 세팅하자
	bool Init(LogConfig& logConfig);

	// 실제로 로그를 출력하는 함수
	void LogOutput(eLogInfoType logInfoType, wchar_t* outputString);

	// 가장 최근에 발생한 에러를 메시지 박스로 출력
	void LogOutputLastErrorToMsgBox(wchar_t* outputString);

	// 로그 설정들을 초기화하고
	// 열려있는 자원을 닫아준다.
	void CloseAllLog();

	// 일정 tick이 지나면,
	// 부모 class에서 OnProcess()를 호출하는데
	// 가상 함수 테이블을 참조하여
	// 자식 class에서 재정의한 OnProcess()가 호출
	void OnProcess() override;

	// 로그를 출력할 다른 윈도우 핸들 세팅
	void SetHwnd(HWND hWnd);

	// 로그 메시지를 관리하는 큐의 현재 크기
	int GetQueueSize();

	// 서버가 연산하고 있는 중에
	// 로그 출력까지 다 하게 된다면,
	// 파일이나 DB에 출력하는 경우는 시간이 오래 걸리는 작업이라서
	// 기다리는데 오랜 시간이 걸린다.
	// 그래서 Log를 출력하지 않고 queue에 넣어두고
	// 다른 thread가 일정 시간마다
	// queue를 검사하여 log를 출력하는 방식으로 동작
	void InsertMsgToQueue(LogMsg* pLogMsg);

private:
	// 매체에 로그를 출력하기 위한 동작
	void OutputFile(wchar_t* outputString);
	void OutputDB(wchar_t* outputString);
	void OutputWindow(eLogInfoType logInfoType, wchar_t* outputString);
	void OutputDebugger(wchar_t* outputString);
	void OutputUDP(eLogInfoType logInfoType, wchar_t* outputString);
	void OutputTCP(eLogInfoType logInfoType, wchar_t* outputString);

	// 출력할 매체 초기화
	bool InitFile();
	bool InitDB();
	bool InitUDP();
	bool InitTCP();

private:
	// LogConfig를 참조하여 값 세팅
	int mLogInfoTypes[MAX_STORAGE_TYPE];
	wchar_t mLogFileName[MAX_FILENAME_LENGTH];
	eLogFileType mLogFileType;

	char mIP[MAX_IP_LENGTH];
	int mUDPPort;
	int mTCPPort;

	char mDSNName[MAX_DSN_NAME];
	char mDSNID[MAX_DSN_ID];
	char mDSNPW[MAX_DSN_PW];

	wchar_t mOutString[MAX_OUTPUT_LENGTH];
	HWND mHwnd;

	HANDLE mLogFile;

	SOCKET mUDPSocket;
	SOCKET mTCPSocket;

	// 출력할 로그를 모아 놓은 queue
	// LogMsg가 실제로 존재하는 곳은 gLogMsg
	Queue<LogMsg*> mLogMsgQueue;

	DWORD mFileMaxSize;
};

// 로그를 남기고 출력할 때
// 접근하기 위한 전역 변수
static wchar_t gOutString[MAX_OUTPUT_LENGTH];
static LogMsg gLogMsg[MAX_QUEUECOUNT];
static Monitor gLogSyncObject;

// 로그를 출력하기 위해서 외부에서 사용하는 함수

bool NETLIB_API INIT_LOG(LogConfig& logConfig);

// 로그를 남기는 함수
void NETLIB_API LOG(eLogInfoType logInfoType, const wchar_t* outputString, ...);

// 가장 최근 에러를 메시지 박스로 출력하는 함수
void NETLIB_API LOG_LASTERROR(wchar_t* outputString, ...);
void NETLIB_API CLOSE_LOG();