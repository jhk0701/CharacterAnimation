#pragma once

#include <memory>
#include "define.h"

class Scene;

class SceneManager 
{
	SINGLETON(SceneManager)

private:
	std::shared_ptr<Scene> m_Scene; // 현재 켜진 씬

public:
	void Update(float delta);
	void Render();
};