#include "Scene.h"
#include "Object.h"

Scene::Scene()
{
	m_vecObj.reserve(100);
}

Scene::~Scene()
{
}

void Scene::Update(float delta)
{
	for (std::shared_ptr<Object> pObj : m_vecObj)
		if (pObj)
			pObj->Update(delta);
}

void Scene::Render()
{
	for (std::shared_ptr<Object> pObj : m_vecObj)
		if (pObj)
			pObj->Render();
}

void Scene::AddObject(std::shared_ptr<Object> pNewObj)
{
	m_vecObj.push_back(pNewObj);
}

void Scene::Destroy(Object* pObj)
{
	pObj->SetEnable(false);

	for (int i = 0; i < m_vecObj.size(); ++i)
	{
		if (m_vecObj[i].get() != pObj)
			continue;
	
		m_vecObj[i] = nullptr;
		return;
	}
}
