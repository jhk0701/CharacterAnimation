#include "pch.h"
#include "GUIManager.h"
#include "CommandContext.h"
#include "Display.h"

GUIManager::GUIManager() {};
GUIManager::~GUIManager() {};

bool GUIManager::Init(HWND hWnd, ID3D12Device* pDevice, ID3D12CommandQueue* pCommandQueue, float w, float h)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(w, h);
	ImGui::StyleColorsDark();

	InitGuiDesc(pDevice); // imgui�� SrvDescHeap �ʱ�ȭ

	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = pDevice;
	initInfo.CommandQueue = pCommandQueue;
	initInfo.NumFramesInFlight = 3; //  Display::SWAP_CHAIN_BUFFER_COUNT
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // g_OverlayBuffer 포맷과 일치해야 함
	initInfo.SrvDescriptorHeap = m_pGuiDesc;
	// 단일 디스크립터 힙(레거시) 경로: 폰트 아틀라스 SRV용 CPU/GPU 핸들 지정
	initInfo.LegacySingleSrvCpuDescriptor = m_pGuiDesc->GetCPUDescriptorHandleForHeapStart();
	initInfo.LegacySingleSrvGpuDescriptor = m_pGuiDesc->GetGPUDescriptorHandleForHeapStart();

	if (!ImGui_ImplDX12_Init(&initInfo))
		return false;

	if (!ImGui_ImplWin32_Init(hWnd))
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
	ImGui_ImplDX12_NewFrame(); // GUI ������ ����
	ImGui_ImplWin32_NewFrame();

	// �׽�Ʈ
	ImGui::NewFrame();

	ImGui::Begin("Test GUI");
	ImGui::Text("Hello World");

	ImGui::End();
	ImGui::Render();
}

void GUIManager::RenderGUI(GraphicsContext& gfxContext)
{
 	ID3D12GraphicsCommandList* pCmdList = gfxContext.GetGraphicsContext().GetCommandList();
	pCmdList->SetDescriptorHeaps(1, &m_pGuiDesc);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCmdList);
}