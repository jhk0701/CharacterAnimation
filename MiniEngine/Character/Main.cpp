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

// �׽�Ʈ�� ��Ŭ���
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

// �׽�Ʈ��
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
};

CREATE_APPLICATION( Character )

void Character::Startup( void )
{
    // Setup your data
    EngineCore::GetInstance()->Init();
}

void Character::Cleanup( void )
{
    // Free up resources in an orderly fashion
    EngineCore::GetInstance()->Clear();
}

void Character::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    // Update something
    EngineCore::GetInstance()->Update(deltaT);
}

void Character::RenderScene( void )
{
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    // 회색 배경
    float ClearGray[4] = { 100.0f / 255.0f, 100.0f / 255.0f, 100.0f / 255.0f, 1.0f };

    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.ClearColor(g_SceneColorBuffer, ClearGray); 
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());

    gfxContext.Finish();
}

#pragma region Test Code

namespace Graphics { extern EnumVar DebugZoom; }

ExpVar g_SunLightIntensity("Viewer/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
NumVar g_SunOrientation("Viewer/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f);
NumVar g_SunInclination("Viewer/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f);

#pragma endregion

