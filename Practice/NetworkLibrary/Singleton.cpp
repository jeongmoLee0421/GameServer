#include "Singleton.h"

std::list<Singleton*> Singleton::mSingletonList{};

Singleton::Singleton()
{
	mSingletonList.push_back(this);
}

Singleton::~Singleton()
{
	// singleton 객체가 ReleaseInstance()를 호출해서
	// instance를 delete하면,
	// 자식 소멸자가 먼저 불리고
	// Finalize()가 호출되면서,
	// 마무리 작업을 하고
	// 부모 소멸자가 불리면,
	// list에서 instance를 가리키는 포인터를 찾아서 지워줘야 하는데
	// 어차피 ReleaseAll() 함수에서
	// 무조건 가장 뒤에 것부터 지우기 때문에
	// 역시 가장 뒤에 것을 erase했다.

	auto singletonIter = mSingletonList.end();
	--singletonIter;
	mSingletonList.erase(singletonIter);
}

void Singleton::ReleaseAll()
{
	// ReleaseInstance() 함수 호출 후에
	// Singleton class의 소멸자가 호출되면,
	// list.erase()를 호출하여 요소를 삭제하는데
	// 이 때 해당 요소를 가리키는 iterator는 무효화가 되고
	// 다음 요소를 가리키는 iterator를 반환해준다.
	
	// 다음 요소를 가리키는 iterator를 받으면,
	// 문제 없이 반복문을 돌 수 있지만,
	// 소멸자에서 iterator를 반환할 수 없기 때문에
	// 아래처럼 사용하면,
	// 무효한 iterator에 연산을 하기 때문에 에러가 발생한다.
	/*for (auto it = mSingletonList.begin(); it != mSingletonList.end(); ++it)
	{
		(*it)->ReleaseInstance();
	}*/

	// 해결 방법으로
	// 매번 새롭게 list의 iterator를 받아서
	// 삭제된 list의 무효한 iterator를 사용하지 않았다.
	while (!mSingletonList.empty())
	{
		auto singletonIter = mSingletonList.end();
		--singletonIter;
		(*singletonIter)->ReleaseInstance();
	}

	// 뒤부터 지운 이유는
	// 소멸은 생성의 역순이라는 규칙을 지키기 위해서이다.
	// 어떤 객체의 생성이 앞서서 생성된 객체에
	// 의존하는 관계일 수 있는데,
	// 소멸할 때 의존하고 있는 관계인 객체를 먼저 지우면,
	// 문제가 발생할 여지가 있다.
	// 그래서 나는 소멸할 때는 생성의 역순으로 한다.
}
