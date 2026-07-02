#include "pch.h"
#include "GUIManager.h"

GUIManager::GUIManager() {};
GUIManager::~GUIManager() {};

bool GUIManager::Init(HWND* pHwnd, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, float w, float h)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(w, h);
	ImGui::StyleColorsDark();

	ImGui_ImplDX12_InitInfo initInfo;
	initInfo.Device = pDevice;
	initInfo.CommandQueue = pCommandQueue;
	initInfo.NumFramesInFlight = 2;
	initInfo.RTVFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	if (!ImGui_ImplDX12_Init(&initInfo))
		return false;

	if (!ImGui_ImplWin32_Init(pHwnd))
		return false;

	return true;
}

void GUIManager::Clear()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void GUIManager::UpdateGUI()
{
	ImGui_ImplDX12_NewFrame(); // GUI 프레임 시작
	ImGui_ImplWin32_NewFrame();

	// 테스트
	ImGui::NewFrame();

	ImGui::Begin("Test GUI");
	ImGui::Text("Hello World");

	ImGui::ShowDemoWindow();

	ImGui::End();
	ImGui::Render();
}

void GUIManager::RenderGUI(ID3D12GraphicsCommandList* pCommandList)
{
	pCommandList->SetDescriptorHeaps(1, );
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);
}
