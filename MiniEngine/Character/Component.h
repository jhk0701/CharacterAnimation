#pragma once

class Component 
{
public:
	Component();
	virtual ~Component();

private:
	bool bIsEnable{ true };

public:
	virtual void Update(float delta) = 0;

	void SetEnable(bool bIsOn) { bIsEnable = bIsOn; }
	bool IsEnable() const { return bIsEnable; }
};