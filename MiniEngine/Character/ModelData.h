#pragma once

// Character 자체 FBX 렌더 경로 - 스킨 메시 로더 + CPU 스키닝.
// FBX SDK로 직접 파싱하여 스켈레톤/스킨/애니메이션을 추출하고, 매 프레임 CPU에서
// 선형 블렌드 스키닝(LBS)으로 정점을 계산한다. 결과는 동적 정점버퍼로 업로드한다.
// Model 프로젝트(Renderer/ModelData/.mini)와 무관한 최소 구현이다.

#include <string>
#include <memory>
#include <DirectXMath.h>
#include <fbxsdk.h> // TOOD : FBX 모델에 국한된 사용 -> Assimp로 바꿀 것

#include "Math/BoundingSphere.h"

class ModelData
{
public:
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 normal;
    };

    ModelData();
    ~ModelData();

    // 스킨/애니메이션 포함 FBX 로드. 성공 시 true. (씬은 런타임 본 평가를 위해 보관.)
    bool Load(const std::wstring& filePath);

    // 애니메이션 시간 진행(루프) + 이번 프레임 스킨드 정점 계산.
    void Update(float deltaT);

    bool IsLoaded() const { return m_vertexCount > 0; }

    // 이번 프레임 CPU 스키닝된 전개 정점(비인덱스, 삼각형 리스트).
    const Vertex* SkinnedVertices() const;
    uint32_t VertexCount() const { return m_vertexCount; }

    const Math::BoundingSphere& GetBoundingSphere() const { return m_boundingSphere; }

    // FBX SDK FbxManager 리소스 해제 (프로그램 종료 시 1회).
    static void Shutdown();

    struct Impl;   // 구현 세부(불투명). .cpp에서 정의.

    void SetAnim(const ModelData& animModel);

private:
    std::unique_ptr<Impl> m_impl;
    uint32_t m_vertexCount = 0;
    Math::BoundingSphere m_boundingSphere = Math::BoundingSphere(Math::kZero);
};
