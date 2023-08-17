#pragma once

// 2023 08 17 이정모 home

// 동기화를 위한 class로
// 내부적으로 CRITICAL_SECTION을 사용

#ifdef NETWORKLIBRARY_EXPORTS
#define NETLIB_API __declspec(dllexport)
#else
#define NETLIB_API __declspec(dllimport)
#endif

#define _WINSOCKAPI_
#include <Windows.h>

// 하나의 객체를 여러 thread에서 병렬적으로 사용할 때
// 멤버 변수에 동기화가 필요하다.
// 이 때 Monitor class를 멤버 변수로 두고
// Owner class 생성자/소멸자를 이용해서
// RAII 메커니즘을 적용하면,
// 동기화가 가능하다
class NETLIB_API Monitor
{
public:
	// Owner class는 생성자로 Monitor를 받아서
	// 생성 시 Monitor.Enter()를 호출하여 lock을 걸고
	// 소멸 시 Monitor.Leave()를 호출하여 lock을 푼다.
	// Monitor class에 대해서 Enter(), Leave()를 호출하지 않더라도
	// Owner 객체 생성/소멸에서 알아서 호출되기 때문에
	// 코드 간소화, lock/unlock 누락 방지 등의 효과가 있다.
	class NETLIB_API Owner
	{
	public:
		Owner(Monitor& crit);
		~Owner();

		Owner(const Owner& rhs) = delete;
		Owner(Owner&& rhs) noexcept = delete;

		Owner& operator=(const Owner& rhs) = delete;
		Owner& operator=(Owner&& rhs) noexcept = delete;

	private:
		Monitor& mSyncObject;
	};

	Monitor();
	~Monitor();

	void Enter();
	void Leave();

	// 복사 생성, 복사 대입 연산을 막은 이유는
	// Owner 객체 생성할 때 Monitor 객체를 참조로 받으면
	// 동일한 Monitor 객체에 대해서 lock, unlock을 수행하는건데
	// 만약 복사로 받게되면,
	// 각 thread마다 다른 Monitor 객체에 대해 lock, unlock을 수행하기 때문에
	// 동기화가 불가능하다.
	Monitor(const Monitor& rhs) = delete;
	Monitor(Monitor&& rhs) noexcept = delete;

	// 이동 생성, 이동 대입 연산을 막은 이유는
	// 이동 연산의 목적이 자원(포인터)에 대한 소유권을 포기하고(nullptr 대입)
	// 다른 객체에게 자원의 소유권을 넘기겠다는 것인데
	// 동일한 monitor 객체에 대해서 lock, unlock을 수행해야 하는데
	// 이동 연산이 발생해서 monitor 객체 소유권을 포기하면,
	// 소유권을 포기한 monitor 객체는 더 이상 접근이 불가능하기 때문에
	// 동기화가 불가능하다.
	Monitor& operator=(const Monitor& rhs) = delete;
	Monitor& operator=(Monitor&& rhs) noexcept = delete;

private:
	CRITICAL_SECTION mSyncObject;
};