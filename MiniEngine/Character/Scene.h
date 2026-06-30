#pragma once

#include <vector>
#include <memory>

class Object;
class Scene 
{
public:
	Scene();
	virtual ~Scene();
	
private:
	std::vector<std::shared_ptr<Object>> m_vecObj;

public:
	void Update(float delta);
	void Render();

	void AddObject(std::shared_ptr<Object> pNewObj);
	
	template<typename T>
	std::weak_ptr<T> Instantiate();
	void Destroy(Object* pObj);
};

template<typename T>
inline std::weak_ptr<T> Scene::Instantiate()
{
	std::shared_ptr<T> pInstance = std::make_shared<T>();
	std::shared_ptr<Object> pObj = std::dynamic_pointer_cast<Object>(pObj);
	
	if (pObj == nullptr)
		return nullptr;

	pObj->SetEnable(true);
	AddObject(pObj);

	return pObj;
}
