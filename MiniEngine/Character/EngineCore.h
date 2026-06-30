#pragma once

#include "define.h"

class EngineCore
{
	SINGLETON(EngineCore)
public:
	void Init();
	void Clear();
	void Update(float delta);
	void Render();
};