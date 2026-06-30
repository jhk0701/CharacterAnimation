#pragma once

#include <memory>

#include "Math/Transform.h"
using namespace Math;

class Object : public std::enable_shared_from_this<Object>
{
public:
	Object();
	virtual ~Object();

private:
	AffineTransform m_Transform;
	bool m_bIsEnable;

public:
	virtual void Update(float delta) = 0;
	virtual void Render() = 0;

	void SetEnable(bool bIsOn) { m_bIsEnable = bIsOn; }
	bool IsEnable() const { return m_bIsEnable; }

	inline Vector3 GetTranslation() const { return m_Transform.GetTranslation(); }
	inline void SetTranslation(const Vector3& _w) { m_Transform.SetTranslation(_w); }
};