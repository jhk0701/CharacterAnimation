#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "GameInput.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "BufferManager.h"

// 테스트용 인클루드
#include "MotionBlur.h"
#include "TemporalEffects.h"
#include "FXAA.h";
#include "PostEffects.h"
#include "SSAO.h"

#include "Model.h"
#include "ModelLoader.h"
#include "FBX.h"
#include "Display.h"
#include "Renderer.h"

#include "Camera.h"
#include "CameraController.h"
#include "ShadowCamera.h"

#include "EngineCore.h"

using namespace GameCore;
using namespace Graphics;

// 테스트용
using Renderer::MeshSorter;

class Character : public GameCore::IGameApp
{
public:

    Character()
    {
    }

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

private:
    // 테스트
    Camera m_Camera;
    std::unique_ptr<CameraController> m_CameraController;

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT     m_MainScissor;

    ModelInstance m_ModelInst;
    ShadowCamera  m_SunShadowCamera;

    void TempStartUp();
    void TempCleanUp();
    void TempUpdate(float deltaT);
    void TempRender(GraphicsContext& gfxContext, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor);
};

CREATE_APPLICATION( Character )

void Character::Startup( void )
{
    // Setup your data
    EngineCore::GetInstance()->Init();
    
    TempStartUp(); // 테스트

    
}

void Character::Cleanup( void )
{
    // Free up resources in an orderly fashion
    EngineCore::GetInstance()->Clear();

    TempCleanUp(); // 테스트
    
    Renderer::Shutdown();
    FBX::Shutdown();
}

void Character::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    // Update something
    EngineCore::GetInstance()->Update(deltaT);

    TempUpdate(deltaT); // 테스트
}

void Character::RenderScene( void )
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();
    const D3D12_VIEWPORT& viewport = m_MainViewport;
    const D3D12_RECT& scissor = m_MainScissor;

    TempRender(gfxContext, viewport, scissor);

    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.ClearColor(g_SceneColorBuffer);
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());
    gfxContext.SetViewportAndScissor(0, 0, g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());

    // Rendering something
    // 렌더링 관련 절차 확인 필요

    gfxContext.Finish();
}


#pragma region Test Code

namespace Graphics { extern EnumVar DebugZoom; }

ExpVar g_SunLightIntensity("Viewer/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
NumVar g_SunOrientation("Viewer/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f);
NumVar g_SunInclination("Viewer/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f);

void Character::TempStartUp()
{
    // 테스트용 임시 세팅 - model viewer와 동일
    // sponza 대신 테스트용 fbx 로드
    MotionBlur::Enable = false;
    TemporalEffects::EnableTAA = true;
    FXAA::Enable = false;
    PostEffects::EnableHDR = true;
    PostEffects::EnableAdaptation = true;
    SSAO::Enable = true;

    Renderer::Initialize();

    std::wstring fbxFileName;
    bool forceRebuild = false;

    uint32_t rebuildValue;
    if (CommandLineArgs::GetInteger(L"rebuild", rebuildValue))
        forceRebuild = rebuildValue != 0;

    if (!CommandLineArgs::GetString(L"cube", fbxFileName))
        fbxFileName = L"Assets/cube.fbx";

    m_ModelInst = Renderer::LoadModel(fbxFileName, forceRebuild);
    if (!m_ModelInst.IsNull())
    {
        m_ModelInst.LoopAllAnimations();
        m_ModelInst.Resize(1.0f);
    }

    m_Camera.SetZRange(1.0f, 10000.0f);
    m_CameraController.reset(new OrbitCamera(
        m_Camera,
        m_ModelInst.IsNull() ? BoundingSphere(Vector3(kZero), 5.0f) : m_ModelInst.GetBoundingSphere(),
        Vector3(kYUnitVector)
    ));
}

void Character::TempCleanUp()
{
    m_ModelInst = nullptr;
}

void Character::TempUpdate(float deltaT)
{
    if (GameInput::IsFirstPressed(GameInput::kLShoulder))
        DebugZoom.Decrement();
    else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
        DebugZoom.Increment();

    m_CameraController->Update(deltaT);

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");
    if (!m_ModelInst.IsNull())
        m_ModelInst.Update(gfxContext, deltaT);
    gfxContext.Finish();

    TemporalEffects::GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);
    m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    m_MainViewport.MinDepth = 0.0f;
    m_MainViewport.MaxDepth = 1.0f;

    m_MainScissor.left = 0;
    m_MainScissor.top = 0;
    m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
}

void Character::TempRender(GraphicsContext& gfxContext, const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor)
{
    if (!m_ModelInst.IsNull())
    {
        float costheta = cosf(g_SunOrientation);
        float sintheta = sinf(g_SunOrientation);
        float cosphi = cosf(g_SunInclination * 3.14159f * 0.5f);
        float sinphi = sinf(g_SunInclination * 3.14159f * 0.5f);

        Vector3 SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

        m_SunShadowCamera.UpdateMatrix(
            -SunDirection, Vector3(0, -500.0f, 0), Vector3(5000, 3000, 3000),
            (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

        GlobalConstants globals;
        globals.ViewProjMatrix = m_Camera.GetViewProjMatrix();
        globals.SunShadowMatrix = m_SunShadowCamera.GetShadowMatrix();
        globals.CameraPos = m_Camera.GetPosition();
        globals.SunDirection = SunDirection;
        globals.SunIntensity = Vector3(Scalar(g_SunLightIntensity));

        gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
        gfxContext.ClearDepth(g_SceneDepthBuffer);

        MeshSorter sorter(MeshSorter::kDefault);
        sorter.SetCamera(m_Camera);
        sorter.SetViewport(viewport);
        sorter.SetScissor(scissor);
        sorter.SetDepthStencilTarget(g_SceneDepthBuffer);
        sorter.AddRenderTarget(g_SceneColorBuffer);

        m_ModelInst.Render(sorter);
        sorter.Sort();

        {
            ScopedTimer _prof(L"Depth Pre-Pass", gfxContext);
            sorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
        }

        SSAO::Render(gfxContext, m_Camera);

        if (!SSAO::DebugDraw)
        {
            ScopedTimer _outerprof(L"Main Render", gfxContext);

            {
                ScopedTimer _prof(L"Sun Shadow Map", gfxContext);
                MeshSorter shadowSorter(MeshSorter::kShadows);
                shadowSorter.SetCamera(m_SunShadowCamera);
                shadowSorter.SetDepthStencilTarget(g_ShadowBuffer);

                m_ModelInst.Render(shadowSorter);
                shadowSorter.Sort();
                shadowSorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
            }

            gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
            gfxContext.ClearColor(g_SceneColorBuffer);

            {
                ScopedTimer _prof(L"Render Color", gfxContext);
                gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
                gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
                gfxContext.SetViewportAndScissor(viewport, scissor);

                sorter.RenderMeshes(MeshSorter::kOpaque, gfxContext, globals);
            }

            Renderer::DrawSkybox(gfxContext, m_Camera, viewport, scissor);
            sorter.RenderMeshes(MeshSorter::kTransparent, gfxContext, globals);
        }
    }
    else
    {
        gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
        gfxContext.ClearColor(g_SceneColorBuffer);
    }

    MotionBlur::GenerateCameraVelocityBuffer(gfxContext, m_Camera, true);
    TemporalEffects::ResolveImage(gfxContext);

    //if (DepthOfField::Enable)
    //    DepthOfField::Render(gfxContext, m_Camera.GetNearClip(), m_Camera.GetFarClip());
    //else
    //    MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer);
}

#pragma endregion

