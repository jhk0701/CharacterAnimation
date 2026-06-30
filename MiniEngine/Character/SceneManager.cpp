#include "pch.h"
#include "SceneManager.h"
#include "Scene.h"

SceneManager::SceneManager() {};
SceneManager::~SceneManager() {};

void SceneManager::Update(float delta)
{
	if (m_Scene)
		m_Scene->Update(delta);
}

void SceneManager::Render() 
{
	if (m_Scene)
		m_Scene->Render();
}