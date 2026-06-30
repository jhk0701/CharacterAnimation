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

#include "EngineCore.h"

using namespace GameCore;
using namespace Graphics;

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

    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.ClearColor(g_SceneColorBuffer);
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());
    gfxContext.SetViewportAndScissor(0, 0, g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());

    // Rendering something
    // 렌더링 관련 절차 확인 필요

    gfxContext.Finish();
}
