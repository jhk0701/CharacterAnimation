#include "pch.h"
#include "Component.h"

Component::Component()
{
}

Component::~Component()
{
}

void Component::Update(float delta)
{
	if (!IsEnable())
		return;
}

void Component::Render()
{
	if (!IsEnable())
		return;
}
