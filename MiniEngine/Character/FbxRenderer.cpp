#include "pch.h"
#include "FbxRenderer.h"
#include "FbxModel.h"

#include "CommandContext.h"
#include "GraphicsCommon.h"
#include "BufferManager.h"
#include "Camera.h"

#include "CompiledShaders/SimpleVS.h"
#include "CompiledShaders/SimplePS.h"

using namespace DirectX;
using namespace Graphics;

namespace
{
    // SimpleVS/PS.hlsl 의 cbuffer(b0) 레이아웃과 일치해야 한다.
    __declspec(align(16)) struct SimpleConstants
    {
        Math::Matrix4 ViewProj;    // 64 bytes (offset 0)
        XMFLOAT3      SunDirection; // 12 bytes (offset 64)
        float         pad0;         // 4 bytes  (offset 76)
        XMFLOAT3      BaseColor;    // 12 bytes (offset 80)
        float         pad1;         // 4 bytes  (offset 92) -> 96
    };
}

void FbxRenderer::Initialize()
{
    m_RootSig.Reset(1, 0);
    m_RootSig[0].InitAsConstantBuffer(0); // b0
    m_RootSig.Finalize(L"FBX RootSig",
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    m_PSO.SetRootSignature(m_RootSig);
    m_PSO.SetInputLayout(_countof(inputLayout), inputLayout);
    m_PSO.SetVertexShader(g_pSimpleVS, sizeof(g_pSimpleVS));
    m_PSO.SetPixelShader(g_pSimplePS, sizeof(g_pSimplePS));
    // 최소 뷰어: FBX->DirectX 축 변환에 따른 와인딩 반전과 무관하게 항상 보이도록 컬링 해제.
    m_PSO.SetRasterizerState(RasterizerTwoSided);
    m_PSO.SetBlendState(BlendDisable);
    m_PSO.SetDepthStencilState(DepthStateReadWrite);
    m_PSO.SetSampleMask(0xFFFFFFFF);
    m_PSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_PSO.SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
    m_PSO.Finalize();
}

void FbxRenderer::Render(GraphicsContext& ctx, const Math::BaseCamera& camera, const FbxModel& model)
{
    if (!model.IsLoaded())
        return;

    SimpleConstants cb;
    cb.ViewProj = camera.GetViewProjMatrix();
    // 카메라 정면-상단에서 오는 광원(-SunDir 방향으로 조명) -> 정면이 밝게 보임.
    XMStoreFloat3(&cb.SunDirection,
        XMVector3Normalize(XMVectorSet(0.3f, -0.7f, -1.0f, 0.0f)));
    cb.pad0 = 0.0f;
    cb.BaseColor = XMFLOAT3(0.7f, 0.7f, 0.7f);   // 흰색 diffuse
    cb.pad1 = 0.0f;

    ctx.SetRootSignature(m_RootSig);
    ctx.SetPipelineState(m_PSO);
    ctx.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.SetDynamicConstantBufferView(0, sizeof(cb), &cb);
    // 매 프레임 CPU 스키닝된 전개 정점을 동적 VB로 업로드(비인덱스 드로우).
    ctx.SetDynamicVB(0, model.VertexCount(), sizeof(FbxModel::Vertex), model.SkinnedVertices());
    ctx.DrawInstanced(model.VertexCount(), 1, 0, 0);
}
