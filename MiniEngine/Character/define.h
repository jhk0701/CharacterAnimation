#pragma once

#define SINGLETON(Type)\
public:\
	static Type* GetInstance()\
	{\
		static Type instance;\
		return &instance;\
	}\
protected:\
	Type();\
	~Type();