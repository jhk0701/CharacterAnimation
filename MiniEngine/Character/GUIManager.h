#pragma once

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

class GUIManager
{
	SINGLETON(GUIManager)

public:
	bool Init(HWND hWnd, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, float w, float h);
	void Clear();

	void UpdateGUI();
	void RenderGUI(GraphicsContext& gfxContext);

private:
	ID3D12DescriptorHeap* m_pGuiDesc;

	void InitGuiDesc(ID3D12Device* pDevice);
};