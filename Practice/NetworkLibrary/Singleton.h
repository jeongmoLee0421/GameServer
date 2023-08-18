#pragma once

// 2023 08 18 이정모 home

// 오직 하나의 객체만 생성되어야 하며,
// 여기저기서 쉽게 접근할 수 있어야 하는 class

// 템플릿을 export할 때 발생하는 경고로
// 템플릿을 dll 내부에서만 사용한다면, 문제가 없다고 한다.
#pragma warning(disable:4251)

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#define NETLIB_TEMPLATE
#else
#define NETLIB_API __declspec(dllimport)
#define NETLIB_TEMPLATE extern
#endif

// mSingletonList에서 중간에 위치한
// singleton 객체가 삭제되는 경우도 있기 때문에 list 사용
// vector 같은 배열일 때 중간에 위치한 데이터가 삭제되면,
// 그 뒤에 있는 모든 데이터를 한 칸씩 앞으로 당겨야해서
// 삭제 연산에서 복사 비용이 최대 O(N)이다.
#include <list>

// Singleton 패턴을 사용하는 class에 필요한
// 멤버 함수 및 멤버 변수 선언
#define DECLEAR_SINGLETON(className)\
public:\
/*
	객체를 내부에서 생성하고 또 객체에 대한 포인터를 반환하는 함수를 만들어야 하는데
	그 함수가 만약 this 포인터가 필요한 비정적 멤버 함수라면,
	객체를 외부에서 만들지 못하게 했기 때문에 this 포인터를 구해올 수가 없고
	결국 호출이 불가능하다.
	그래서 this 포인터가 없어도 호출 가능한 정적(static) 멤버 함수로 만들었다.
*/\
static className* GetInstance(); \
\
/*
Singleton 객체마다 소멸시켜야 하는 instance가 자식 class에 위치하고 있다.
부모 class는 자식 class의 멤버 변수를 알 수 없기 때문에
instnace를 소멸시키기 위해서는 결국 자식 class의 입장이 되어야 한다.
그래서 ReleaseInstance()를 가상 함수로 만들어서
가상 함수 테이블을 참조하여, 자식 class에서 재정의한 함수가 호출되도록 만들었다.
*/\
void ReleaseInstance() override; \
\
private:\
	/*
	Singleton 객체는 오직 하나만 존재해야 하기 때문에
	외부에서 객체를 함부로 생성하거나 소멸하는 행위를 막아야 한다.
	그래서 생성자/소멸자를 private으로 선언해서 외부에서 호출을 불가능하게 했다.
	*/\
	className() {} \
	~className() {}\
	\
private:\
	/*
	GetInstance() 함수는 this 포인터 없이 동작하는 정적 멤버 함수로
	this 포인터가 필요한 비정적 멤버 변수는 접근이 불가능하다.
	그래서 실제 객체에 대한 포인터를 담을 변수는
	this 포인터가 필요하지 않은 정적 멤버 변수로 선언했다.
	*/\
	static className* mInstance; \

// singleton 객체를 얻어오기 위한 GetInstance() 함수 호출을
// 한번 래핑하여 더 간결하게 쓰기 위함
#define CREATE_FUNCTION(className, funcName)\
static className* funcName()\
{\
	return className::GetInstance();\
}\

// singleton class 실제 구현부
#define IMPLEMENT_SINGLETON(className)\
className* className::mInstance{ nullptr };\
\
className* className::GetInstance()\
{\
	if (!mInstance)\
	{\
		mInstance = new className{};\
	}\
\
	return mInstance;\
}\
\
void className::ReleaseInstance()\
{\
/*
	이것이 원래 코드였으나,

	if (mInstance)
	{
		delete mInstance;
		mInstance = nullptr;
	}

	delete 하려는 객체의 포인터가 nullptr이면,
	delete 연산자를 호출했을 때
	OS가 메모리를 회수 연산을 하지 않는다.
*/\
	delete mInstance;\
	mInstance = nullptr;\
}\

// Sington 패턴을 사용하는 모든 객체를
// 관리하기 위한 class이다.
class NETLIB_API Singleton
{
public:
	Singleton();
	virtual ~Singleton();

public:
	// 순수 가상 함수를 통해서 자식 class에서 재정의를 강제했다.
	// 가상 함수 테이블을 사용한 다형성을 통해서
	// 자식 class에서 재정의한 ReleaseInstance()가 호출되고
	// 자식 class 멤버 변수인 mInstance를 delete한다.
	virtual void ReleaseInstance() = 0;

	// 프로세스가 마무리되는 단계에서
	// 모든 singleton 객체를 소멸시켜야 하는데
	// 어떤 singleton 객체가 살아있고 죽어있는지
	// 하나씩 다 찾는 것은 매우 비효율적이기 때문에
	// SingletonList를 순회하면서 모든 singleton 객체를 소멸시킨다.
	// 
	// Singleton class는 순수 가상 함수를 가진 추상 class이기 때문에
	// 객체 생성이 불가능하고
	// 그래서 this 포인터가 없어도 되는 정적 멤버 함수로 선언했다.
	// 
	// 또한 GetInstance()를 호출하여 가져온 singleton 객체가
	// static 멤버 변수라서 this 포인터가 없기 떄문에
	// 정적 멤버 함수여야 한다.
	static void ReleaseAll();

private:
	// Singleton을 상속하는 모든 객체들을 관리하기 위한 list로
	// 상속할 때 마다 독립적으로 생성되면 안되고
	// Singleton class에 단 하나만 존재해야 하기 때문에
	// static으로 선언해서 공유한다.
	static std::list<Singleton*> mSingletonList;
};

NETLIB_TEMPLATE template class NETLIB_API std::list<Singleton*>;

// C4251 warning
// dll에서 class를 export할 때
// class 내부에서 템플릿을 사용하는 경우
// 템플릿을 export하기 위해서는 추가적인 작업이 필요하다고 한다.
// 
// 템플릿을 명시적 인스턴스화 하고
// dllimport하는 client쪽에서 extern을 선언해서
// 해당 템플릿을 사용하겠다고 지정해야 한다.
//
// 그래서 그렇게 해보았으나,
// STL을 제작한 마이크로소프트가
// STL의 구조적인 문제 때문에 이를 완전히 해결하지 못했다고 한다.
// vector<T> class만 예외적으로 경고가 없다고 한다.
//
// 하지만 이 템플릿을 dll 코드 내부에서만 사용한다면,
// 문제가 발생하지는 않는다고 한다.
//
// https://t1.daumcdn.net/cfile/tistory/2052F1044C25F37889