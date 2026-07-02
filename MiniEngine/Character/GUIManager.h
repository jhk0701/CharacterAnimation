#pragma once

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

class GUIManager
{
	SINGLETON(GUIManager)

public:
	bool Init(HWND* pHwnd, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, float w, float h);
	void Clear();
	void UpdateGUI();

private:
	bool bIsInitialized{ false };

};