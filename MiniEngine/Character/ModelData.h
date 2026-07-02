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

    bool Load(const std::wstring& filePath);
    bool IsLoaded() const { return m_vertexCount > 0; }
    void Update(float deltaT);

    const Vertex* SkinnedVertices() const;
    uint32_t VertexCount() const { return m_vertexCount; }

    const Math::BoundingSphere& GetBoundingSphere() const { return m_boundingSphere; }
    void Shutdown();
    struct Impl; 

    void SetAnim(const ModelData& animModel);

private:
    std::unique_ptr<Impl> m_impl;
    uint32_t m_vertexCount = 0;
    Math::BoundingSphere m_boundingSphere = Math::BoundingSphere(Math::kZero);
};
