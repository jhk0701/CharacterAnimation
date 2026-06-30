#include "Actor.h"
#include "Component.h"

Actor::Actor()
{
	m_vecComp.reserve(10);
}

Actor::~Actor()
{
}

void Actor::Update(float delta)
{
	if (!IsEnable() || IsStatic())
		return;

	for (std::shared_ptr<Component>& pComp : m_vecComp)
		pComp->Update(delta);
}

void Actor::Render()
{
	if (!IsEnable())
		return;
}
