#pragma once

// Character 자체 FBX 렌더 경로 - 파이프라인 + 드로우.
// Core 그래픽스 래퍼(RootSignature/GraphicsPSO/GraphicsContext)만 사용한다.

#include "RootSignature.h"
#include "PipelineState.h"

class GraphicsContext;
class FbxModel;
namespace Math { class BaseCamera; }

class FbxRenderer
{
public:
    // 루트 시그니처 + PSO 생성. Graphics 초기화 이후 1회 호출.
    void Initialize();

    // 대상 모델을 카메라 기준으로 렌더. 렌더 타겟/뷰포트는 호출부에서 설정.
    void Render(GraphicsContext& ctx, const Math::BaseCamera& camera, const FbxModel& model);

private:
    RootSignature m_RootSig;
    GraphicsPSO m_PSO{ L"FBX PSO" };
};
