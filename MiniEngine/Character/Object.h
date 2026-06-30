#pragma once

#include "Math/Transform.h"

using namespace Math;

class Object 
{
public:
	Object();
	virtual ~Object();

private:
	AffineTransform m_Transform;

public:
	virtual void Update() = 0;
	virtual void Render() = 0;

	inline Vector3 GetTranslation() const { return m_Transform.GetTranslation(); }
	inline void SetTranslation(const Vector3& _w) { m_Transform.SetTranslation(_w); }
};