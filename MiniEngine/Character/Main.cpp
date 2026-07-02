#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "SystemTime.h"
#include "GameInput.h"
#include "CommandContext.h"
#include "BufferManager.h"

#include "TemporalEffects.h"
#include "PostEffects.h"

#include "Camera.h"
#include "CameraController.h"

#include "EngineCore.h"
#include "GUIManager.h"

// Character 자체 FBX 직접 렌더 경로 (Model/Renderer 파이프라인 미사용)
#include "FbxModel.h"
#include "FbxRenderer.h"

using namespace GameCore;
using namespace Graphics;

class Character : public GameCore::IGameApp
{
public:

    Character() {}

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

private:
    Camera m_Camera;
    std::unique_ptr<CameraController> m_CameraController;

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT     m_MainScissor;

    FbxModel    m_FbxModel;
    FbxRenderer m_FbxRenderer;
};

CREATE_APPLICATION( Character )

// 테스트용 배경
static float s_ClearGray[4] = { 10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f };

void Character::Startup( void )
{
    EngineCore::GetInstance()->Init();

    // 테스트 세팅
    // 후처리 설정. TAA는 사용하지 않으므로 resolve 불필요.
    PostEffects::EnableHDR = true;
    PostEffects::EnableAdaptation = true;
    TemporalEffects::EnableTAA = false;

    m_FbxRenderer.Initialize();

    if (!m_FbxModel.Load(L"Assets/Capoeira.fbx"))
        Utility::Printf("[Character] Failed to load Assets/Capoeira.fbx\n");

    m_Camera.SetZRange(1.0f, 10000.0f);
    m_CameraController.reset(new OrbitCamera(
        m_Camera,
        m_FbxModel.IsLoaded() ? m_FbxModel.GetBoundingSphere()
                              : Math::BoundingSphere(Math::Vector3(Math::kZero), 5.0f),
        Math::Vector3(Math::kYUnitVector)));

    GUIManager::GetInstance()->Init(GetHwnd(), GetDx12Device(), GetDx12CommandQueue(),
        g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());
}

void Character::Cleanup( void )
{
    EngineCore::GetInstance()->Clear();
    
    FbxModel::Shutdown();

    GUIManager::GetInstance()->Clear();
}

void Character::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    EngineCore::GetInstance()->Update(deltaT);

    m_FbxModel.Update(deltaT);   // 애니메이션 진행 + CPU 스키닝

    m_CameraController->Update(deltaT);

    // TAA 미사용 -> 지터 없이 전체 화면 뷰포트.
    m_MainViewport.TopLeftX = 0.0f;
    m_MainViewport.TopLeftY = 0.0f;
    m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    m_MainViewport.MinDepth = 0.0f;
    m_MainViewport.MaxDepth = 1.0f;

    m_MainScissor.left = 0;
    m_MainScissor.top = 0;
    m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
}

void Character::RenderScene( void )
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    gfxContext.ClearDepth(g_SceneDepthBuffer);

    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.ClearColor(g_SceneColorBuffer, s_ClearGray);   // 회색 배경

    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV());
    gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

    m_FbxRenderer.Render(gfxContext, m_Camera, m_FbxModel);

    gfxContext.Finish();
}
