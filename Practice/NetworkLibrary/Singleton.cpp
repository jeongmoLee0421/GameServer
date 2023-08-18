#include "Singleton.h"

std::list<Singleton*> Singleton::mSingletonList{};

Singleton::Singleton()
{
	mSingletonList.push_back(this);
}

Singleton::~Singleton()
{
	auto singletonIter = mSingletonList.begin();
	while (singletonIter != mSingletonList.end())
	{
		if (this == *singletonIter)
		{
			break;
		}

		++singletonIter;
	}

	// singleton 객체가 ReleaseInstance()를 호출해서
	// instance를 delete하면,
	// 부모 소멸자에서는 해당 singleton 객체의 포인터가 들어있는
	// 노드를 찾아서 삭제한다.
	mSingletonList.erase(singletonIter);
}

void Singleton::ReleaseAll()
{
	for (auto* pSingleton : mSingletonList)
	{
		pSingleton->ReleaseInstance();
	}

	mSingletonList.clear();
}
