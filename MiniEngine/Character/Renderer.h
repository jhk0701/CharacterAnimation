#pragma once

// Character 자체 FBX 렌더 경로 - 파이프라인 + 드로우.

#include "RootSignature.h"
#include "PipelineState.h"

class GraphicsContext;
class ModelData;
namespace Math { class BaseCamera; }

class Renderer
{
public:
    void Initialize();

    // 대상 모델을 카메라 기준으로 렌더. 렌더 타겟/뷰포트는 호출부에서 설정.
    void Render(GraphicsContext& ctx, const Math::BaseCamera& camera, const ModelData& mesh);

private:
    RootSignature m_RootSig;
    GraphicsPSO m_PSO{ L"Mesh PSO" };
};
