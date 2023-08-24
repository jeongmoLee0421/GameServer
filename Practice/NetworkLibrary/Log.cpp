#include <time.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32")

#include "Log.h"

IMPLEMENT_SINGLETON(Log);

// 생성자/소멸자가 private이라서
// 외부에서 직접 호출이 불가능하기 때문에
// Initialize(), Finalize() 함수에
// 생성/소멸 시 해야할 작업을 넣으면,
// 내부 생성자/소멸자에서 호출해줌
void Log::Initialize()
{

}

void Log::Finalize()
{

}

bool Log::Init(LogConfig& logConfig)
{
	time_t currTime{ time(NULL) };
	struct tm localTime {};
	localtime_s(&localTime, &currTime);

	// 어떤 매체에
	// 어떤 등급의 로그를 출력할지에 대한 정보 세팅
	CopyMemory(mLogInfoTypes,
		logConfig.mLogInfoTypes,
		MAX_STORAGE_TYPE * sizeof(int));

	wchar_t strTime[100]{};
	wcsftime(strTime,
		sizeof(strTime),
		L"%m월%d일%H시%M분",
		&localTime);

	// 로그를 출력할 디렉토리 생성
	CreateDirectory(L"./LOG", NULL);

	// 로그를 저장할 파일 이름 세팅
	swprintf_s(mLogFileName,
		sizeof(mLogFileName),
		L"./Log/%s_%s.log",
		logConfig.mLogFileName,
		strTime);

	// log class 멤버 변수에
	// 로그 정보를 세팅
	strncpy_s(mIP,
		sizeof(mIP),
		logConfig.mIP,
		MAX_IP_LENGTH);

	strncpy_s(mDSNName,
		sizeof(mDSNName),
		logConfig.mDSNName,
		MAX_DSN_NAME);

	strncpy_s(mDSNID,
		sizeof(mDSNID),
		logConfig.mDSNID,
		MAX_DSN_ID);

	strncpy_s(mDSNPW,
		sizeof(mDSNPW),
		logConfig.mDSNPW,
		MAX_DSN_PW);

	mLogFileType = logConfig.mLogFileType;
	mTCPPort = logConfig.mTCPPort;
	mUDPPort = logConfig.mUDPPort;
	mFileMaxSize = logConfig.mFileMaxSize;
	mHwnd = logConfig.mHwnd;

	bool ret{ true };

	// 로그를 출력할 매체가 파일인데
	// LOG_NONE이 아니라는 것은
	// 알람 또는 에러 정보를 남기겠다는 의미
	if (static_cast<int>(eLogInfoType::LOG_NONE) !=
		static_cast<int>(mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_FILE)]))
	{
		ret = InitFile();
	}
	if (false == ret)
	{
		CloseAllLog();
		return false;
	}

	// DB로 로그를 출력할건지?
	if (static_cast<int>(eLogInfoType::LOG_NONE) !=
		static_cast<int>(mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_DB)]))
	{
		ret = InitDB();
	}
	if (false == ret)
	{
		CloseAllLog();
		return false;
	}

	// UDP로 로그를 출력할건지?
	if (static_cast<int>(eLogInfoType::LOG_NONE) !=
		static_cast<int>(mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_UDP)]))
	{
		ret = InitUDP();
	}
	if (false == ret)
	{
		CloseAllLog();
		return false;
	}

	// TCP로 로그를 출력할건지?
	if (static_cast<int>(eLogInfoType::LOG_NONE) !=
		static_cast<int>(mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_TCP)]))
	{
		ret = InitTCP();
	}
	if (false == ret)
	{
		CloseAllLog();
		return false;
	}

	// 로그를 출력하기 위한 환경이 세팅되었다면,
	// tick마다 전담해서 로그를 출력하기 위한 thread를 생성
	CreateThread(logConfig.mProcessTick);
	Run();
	return true;
}

void Log::LogOutput(eLogInfoType logInfoType, wchar_t* outputString)
{
	// logInfoType에
	// 로그의 종류(알람, 에러), 로그의 등급 정보가
	// 비트로 저장되어 있는데
	// and 연산을 했을 때 0이 아닌 값이 나왔다면,
	// 해당 로그는 남겨야한다는 의미
	if (mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_UDP)] &
		static_cast<int>(logInfoType))
	{
		OutputUDP(logInfoType, outputString);
	}

	if (mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_TCP)] &
		static_cast<int>(logInfoType))
	{
		OutputTCP(logInfoType, outputString);
	}

	int logInfoTypeIndex = static_cast<int>(logInfoType);


	// 4비트를 왼쪽으로 밀었는데 0이 아니다?
	// LOG_ERROR
	if (0 != (static_cast<int>(logInfoType) >> 4))
	{
		logInfoTypeIndex = (static_cast<int>(logInfoType) >> 4) + 0x10 - 1;
	}

	// 위 if문을 수행하지 않았다면,
	// 하위 4비트가 세팅되어 있다는 뜻이고
	// LOG_INFO인 경우로
	// index값 자체가 string table의 index에 매핑된다.

	if (0 > logInfoTypeIndex || 31 < logInfoTypeIndex)
	{
		return;
	}

	time_t currTime{ time(NULL) };
	struct tm localTime {};

	localtime_s(&localTime, &currTime);

	wchar_t timeStr[40]{};
	wcsftime(timeStr, sizeof(timeStr), L"%Y/%m/%d(%H/%M/%S)", &localTime);

	// 시간 | 정보 형태 | 정보 등급 | 사용자 로그
	swprintf_s(mOutString,
		static_cast<size_t>(sizeof(mOutString) * 0.5), // wchar_t는 sizeof를 하면 2바이트로 잡히기 때문에 버퍼 개수는 바이트 크기의 절반이다
		L"%ws | %ws | %ws | %ws\r\n",
		timeStr,
		static_cast<int>(logInfoType) >> 4 ? L"에러" : L"정보",
		LogInfoType_StringTable[logInfoTypeIndex],
		outputString);

	// 매체에 출력하고자 하는 로그 등급과
	// 현재 출력하고자 하는 로그의 등급이 일치하는지
	// bitwise and 연산을 통해 확인
	// 동일한 위치에 있는 비트가 1, 1이면 결과 비트는 1로 세팅되는 것을 이용
	if (mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_FILE)] &
		static_cast<int>(logInfoType))
	{
		OutputFile(mOutString);
	}

	if (mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_DB)] &
		static_cast<int>(logInfoType))
	{
		OutputDB(mOutString);
	}

	if (mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_WINDOW)] &
		static_cast<int>(logInfoType))
	{
		OutputWindow(logInfoType, mOutString);
	}

	if (mLogInfoTypes[static_cast<int>(eLogStorageType::STORAGE_OUTPUTWND)] &
		static_cast<int>(logInfoType))
	{
		OutputDebugger(mOutString);
	}
}

void Log::LogOutputLastErrorToMsgBox(wchar_t* outputString)
{
	DWORD lastError{ GetLastError() };
	if (0 == lastError)
	{
		return;
	}

	LPVOID pDump{ nullptr };

	// last error를 설명해주는 문자열을
	// 시스템 테이블에서 찾아서
	// OS가 내부적으로 메모리를 할당하면,
	// pDump가 해당 문자열을 가리키도록 함
	int result = FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL,
		lastError,
		MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		reinterpret_cast<LPWSTR>(pDump),
		0,
		NULL
	);

	swprintf_s(
		gOutString,
		static_cast<size_t>(sizeof(gOutString) * 0.5), // gOutString의 배열 개수인 MAX_OUTPUT_LENGTH로 해도 괜찮다.
		L"에러위치: %ws\n에러번호: %d\n설명: %ws",
		outputString,
		lastError,
		reinterpret_cast<wchar_t*>(pDump)
	);

	// 콘솔이면
	printf("%ws\n", gOutString);

	// 창이면
	//MessageBox(NULL, gOutString, L"GetLastError", MB_OK);

	// 에러에 대한 설명을 담은 버퍼를
	// OS가 내부적으로 할당했고
	// pDump로 해당 주소를 가리키고 있다.
	// 다 사용했다면, 해제를 요청하는 것은 당연
	if (result)
	{
		LocalFree(pDump);
	}
}

void Log::CloseAllLog()
{
	ZeroMemory(mLogInfoTypes, MAX_STORAGE_TYPE * sizeof(int));
	ZeroMemory(mLogFileName, MAX_FILENAME_LENGTH);

	mLogFileType = eLogFileType::FILETYPE_NONE;

	ZeroMemory(mIP, MAX_IP_LENGTH);
	mUDPPort = DEFAULT_UDPPORT;
	mTCPPort = DEFAULT_TCPPORT;

	ZeroMemory(mDSNName, MAX_DSN_NAME);
	ZeroMemory(mDSNID, MAX_DSN_ID);
	ZeroMemory(mDSNPW, MAX_DSN_PW);

	mHwnd = NULL;

	// 로그를 출력하기 위한 파일이 열려있다면,
	// 닫아주자
	if (mLogFile)
	{
		CloseHandle(mLogFile);
		mLogFile = NULL;
	}

	if (INVALID_SOCKET != mUDPSocket)
	{
		closesocket(mUDPSocket);
		mUDPSocket = INVALID_SOCKET;
	}

	if (INVALID_SOCKET != mTCPSocket)
	{
		shutdown(mTCPSocket, SD_BOTH);
		closesocket(mTCPSocket);
		mTCPSocket = INVALID_SOCKET;
	}

	// thread 종료
	Stop();
}

void Log::OnProcess()
{
	// tick마다 호출되면,
	// logMsgQueue에 있는 데이터를 읽어서
	// log를 출력

	int logCount = mLogMsgQueue.GetCurrentSize();
	for (int i = 0; i < logCount; ++i)
	{
		// 가져와서
		LogMsg* pLogMsg = mLogMsgQueue.Front();

		// 출력하고
		LogOutput(pLogMsg->mLogInfoType,
			pLogMsg->mOutputString);

		// 데이터 빼주고
		mLogMsgQueue.Pop();
	}
}

void Log::SetHwnd(HWND hWnd)
{
	mHwnd = hWnd;
}

int Log::GetQueueSize()
{
	return mLogMsgQueue.GetCurrentSize();
}

bool Log::InitFile()
{
	mLogFile = CreateFile(
		mLogFileName,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);

	if (NULL == mLogFile)
	{
		return false;
	}

	SetFilePointer(
		mLogFile,
		0,
		0,
		FILE_BEGIN
	);

	// 유니코드 프로젝트라
	// 파일에 유니코드 형태로 작성된다.
	// 하지만 그냥 작성하면, 한글이 깨져서 보인다.
	// 파일이 정확하게 디코딩을 하려면,
	// 이 파일에 어떤 인코딩 방식이 적용되었는지 알려줘야 하는데,
	// UTF-16은 문서 선두에 0xFEFF를 넣어주면 된다.
	unsigned short byteOrderMark{ 0xFEFF };
	DWORD byteWritten{ 0 };

	WriteFile(
		mLogFile,
		&byteOrderMark,
		sizeof(byteOrderMark),
		&byteWritten,
		NULL
	);

	return true;
}

bool Log::InitDB()
{
	// DB는 사용하게되면
	// 코드를 추가하자

	return true;
}

bool Log::InitUDP()
{
	WSADATA wsaData{};

	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret)
	{
		return false;
	}

	return true;
}

bool Log::InitTCP()
{
	WSADATA wsaData{};

	// 성공하면 반환 값이 0
	int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret)
	{
		return false;
	}

	// 이미 열려있는 소켓
	if (INVALID_SOCKET != mTCPSocket)
	{
		return false;
	}

	mTCPSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (INVALID_SOCKET == mTCPSocket)
	{
		return false;
	}

	// 로그를 출력할 곳(연결할 곳)의 정보 세팅
	SOCKADDR_IN addr{};
	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, mIP, &addr.sin_addr.s_addr);
	addr.sin_port = htons(mTCPPort);

	ret = connect(mTCPSocket,
		reinterpret_cast<const sockaddr*>(&addr),
		sizeof(addr));
	if (SOCKET_ERROR == ret)
	{
		return false;
	}

	return true;
}

void Log::InsertMsgToQueue(LogMsg* pLogMsg)
{
	mLogMsgQueue.Push(pLogMsg);
}

void Log::OutputFile(wchar_t* outputString)
{
	if (NULL == mLogFile)
	{
		return;
	}

	wchar_t strTime[100]{};
	DWORD fileSize = GetFileSize(mLogFile, NULL);

	// 파일 크기 제한에 걸렸다면
	if (mFileMaxSize < fileSize || MAX_LOGFILE_SIZE < fileSize)
	{
		time_t currTime{ time(NULL) };
		struct tm localTime {};

		localtime_s(&localTime, &currTime);

		wcsftime(strTime,
			sizeof(strTime),
			L"%m월%d일%H시%M분",
			&localTime);

		// 처음 '_'를 만나기 전까지가 로그 파일의 제목
		int strIndex = 0;
		while (mLogFileName[strIndex] != L'_')
		{
			++strIndex;
		}
		mLogFileName[strIndex] = L'\0';

		swprintf_s(
			mLogFileName,
			sizeof(mLogFileName),
			L"%s_%s.log",
			mLogFileName,
			strTime
		);

		CloseHandle(mLogFile);
		mLogFile = NULL;
		InitFile();
	}

	// 파일 끝으로 파일 포인터 이동
	SetFilePointer(mLogFile, 0, 0, FILE_END);

	DWORD writtenBytes{ 0 };
	WriteFile(mLogFile,
		outputString,
		static_cast<DWORD>(wcslen(outputString) * 2), // wide char는 2바이트
		&writtenBytes,
		NULL);
}

void Log::OutputDB(wchar_t* outputString)
{
	// DB는 사용하게되면 추가
}

void Log::OutputWindow(eLogInfoType logInfoType, wchar_t* outputString)
{
	if (NULL == mHwnd)
	{
		return;
	}

	// 다른 윈도우에게 메시지 전송
	SendMessage(mHwnd,
		WM_DEBUGMSG,
		reinterpret_cast<WPARAM>(outputString),
		static_cast<LPARAM>(logInfoType));
}

void Log::OutputDebugger(wchar_t* outputString)
{
	// visual studio 출력창에 출력
	OutputDebugString(outputString);
}

void Log::OutputUDP(eLogInfoType logInfoType, wchar_t* outputString)
{
	SOCKADDR_IN addr;
	memset(&addr, 0x00, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, mIP, &addr.sin_addr.s_addr);
	addr.sin_port = htons(mUDPPort);

	int bufLength = static_cast<int>(wcslen(outputString));

	if (INVALID_SOCKET == mUDPSocket)
	{
		mUDPSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (INVALID_SOCKET == mUDPSocket)
		{
			return;
		}
	}

	// wchar_t는 2바이트로 표현되기 때문에
	// 보낼 문자열의 길이 * 2에 해당하는 크기의
	// 데이터를 보내면 된다.
	sendto(mUDPSocket,
		reinterpret_cast<const char*>(outputString),
		bufLength * sizeof(wchar_t),
		0,
		reinterpret_cast<const sockaddr*>(&addr),
		sizeof(addr));
}

void Log::OutputTCP(eLogInfoType logInfoType, wchar_t* outputString)
{
	int length = static_cast<int>(wcslen(outputString));

	send(mTCPSocket,
		reinterpret_cast<const char*>(outputString),
		length * sizeof(wchar_t),
		0);
}

bool NETLIB_API INIT_LOG(LogConfig& logConfig)
{
	return Log::GetInstance()->Init(logConfig);
}

void NETLIB_API LOG(eLogInfoType logInfoType, const wchar_t* outputString, ...)
{
	Monitor::Owner lock{ gLogSyncObject };

	int queueCount = Log::GetInstance()->GetQueueSize();
	if (MAX_QUEUECOUNT == queueCount)
	{
		return;
	}

	va_list argPtr{ nullptr };

	// 문자열의 시작 주소를 세팅
	va_start(argPtr, outputString);

	// argPtr이 문자열을 순회하면서
	// 서식문자를 만나면,
	// 뒤에 받은 가변 인자들이 지역 변수로
	// 스택에 쌓여 있기 때문에
	// 이를 참조해서 문자열을 완성해준다.
	vswprintf_s(
		gLogMsg[queueCount].mOutputString,
		sizeof(gLogMsg[queueCount].mOutputString),
		outputString,
		argPtr);

	va_end(argPtr);

	gLogMsg[queueCount].mLogInfoType = logInfoType;

	// gLogMsg가 배열로 선언되어 있고
	// 큐의 크기를 index로 해서
	// gLogMsg[index]에 로그 정보를 세팅하고
	// 메모리 주소를 큐에 넣어준다.
	// 일정 주기마다 OnProcess() 함수가 호출되면,
	// 큐에서 포인터 변수를 꺼내서
	// 해당하는 메모리에 저장되어 있는 데이터를
	// 매체에 출력한다.
	Log::GetInstance()->InsertMsgToQueue(&gLogMsg[queueCount]);
}

void NETLIB_API LOG_LASTERROR(wchar_t* outputString, ...)
{
	va_list argPtr{ nullptr };

	va_start(argPtr, outputString);

	vswprintf_s(
		gOutString,
		static_cast<size_t>(sizeof(gOutString) * 0.5),
		outputString,
		argPtr
	);

	va_end(argPtr);

	Log::GetInstance()->LogOutputLastErrorToMsgBox(gOutString);
}

void NETLIB_API CLOSE_LOG()
{
	Log::GetInstance()->CloseAllLog();
}
