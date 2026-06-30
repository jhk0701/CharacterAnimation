#pragma once

#include "Object.h"

class Component : public Object
{
public:
	Component();
	virtual ~Component();

private:
	bool bIsEnable{ true };

public:
	virtual void Update(float delta) override;
	virtual void Render() override;

	void SetEnable(bool bIsOn) { bIsEnable = bIsOn; }
	bool IsEnable() const { return bIsEnable; }
};