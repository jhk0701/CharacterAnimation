#pragma once

#include "Object.h"

#include <memory>
#include <vector>

class Component;
class Actor : public Object 
{
public:
	Actor();
	virtual ~Actor();

private:
	bool bIsStatic{ false };
	std::vector<std::shared_ptr<Component>> m_vecComp;

public:
	virtual void Update(float delta) override;
	virtual void Render() override;
	
	template<typename T>
	std::weak_ptr<T> AddComponent();

	void SetIsStatic(bool bIsOn) { bIsStatic = bIsOn; }
	bool IsStatic() const { return bIsStatic; }
};

template<typename T>
inline std::weak_ptr<T> Actor::AddComponent()
{
	std::shared_ptr<T> newInstance = std::make_shared<T>();
	std::shared_ptr<Component> newComp = std::dynamic_pointer_cast<Component>(newInstance);
	if (newComp == nullptr)
		return nullptr;

	m_vecComp.push_back(newInstance);

	return newInstance;
}
