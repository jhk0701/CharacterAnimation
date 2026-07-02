#include "pch.h"
#include "GUIManager.h"
#include "CommandContext.h"
#include "Display.h"

GUIManager::GUIManager() {};
GUIManager::~GUIManager() {};

bool GUIManager::Init(HWND* pHwnd, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, float w, float h)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(w, h);
	ImGui::StyleColorsDark();
	
	InitGuiDesc(pDevice); // imgui용 SrvDescHeap 초기화

	ImGui_ImplDX12_InitInfo initInfo;
	initInfo.Device = pDevice;
	initInfo.CommandQueue = pCommandQueue;
	initInfo.NumFramesInFlight = 3; //  Display::SWAP_CHAIN_BUFFER_COUNT
	initInfo.RTVFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	initInfo.SrvDescriptorHeap = m_pGuiDesc;

	if (!ImGui_ImplDX12_Init(&initInfo))
		return false;

	if (!ImGui_ImplWin32_Init(pHwnd))
		return false;

	return true;
}

void GUIManager::InitGuiDesc(ID3D12Device* pDevice)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 1;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pGuiDesc));
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

void GUIManager::RenderGUI(GraphicsContext& gfxContext)
{
 	ID3D12GraphicsCommandList* pCmdList = gfxContext.GetGraphicsContext().GetCommandList();
	pCmdList->SetDescriptorHeaps(1, &m_pGuiDesc);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);

	// OMSetRenderTargets
}