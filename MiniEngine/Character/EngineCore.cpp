#include "EngineCore.h"
#include "SceneManager.h"

EngineCore::EngineCore() {}
EngineCore::~EngineCore() {}


void EngineCore::Init()
{
}

void EngineCore::Clear()
{
}

void EngineCore::Update(float delta)
{
	SceneManager::GetInstance()->Update(delta);
}

void EngineCore::Render()
{
	SceneManager::GetInstance()->Render();
}
