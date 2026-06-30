#include "Object.h"

Object::Object() :
	m_bIsEnable(false)
{
}

Object::~Object()
{
}

void Object::Render()
{
	if (!IsEnable())
		return;
}
