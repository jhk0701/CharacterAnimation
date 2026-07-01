#pragma once

// Character 자체 FBX 렌더 경로 - 정적 메시 로더.
// FBX SDK로 직접 파싱하여 position+normal 정점과 인덱스를 GPU 버퍼로 올린다.
// Model 프로젝트(Renderer/ModelData/.mini)와 무관한 최소 구현이다.

#include <string>
#include <DirectXMath.h>

#include "GpuBuffer.h"
#include "Math/BoundingSphere.h"

class FbxModel
{
public:
    struct Vertex
    {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT3 normal;
    };

    // cube.fbx 등 정적 메시 로드. 성공 시 true.
    bool Load(const std::wstring& filePath);

    bool IsLoaded() const { return m_IndexCount > 0; }

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const { return m_VB.VertexBufferView(0); }
    D3D12_INDEX_BUFFER_VIEW  IndexBufferView() const { return m_IB.IndexBufferView(0); }
    uint32_t IndexCount() const { return m_IndexCount; }

    const Math::BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

    // FBX SDK FbxManager 리소스 해제 (프로그램 종료 시 1회).
    static void Shutdown();

private:
    ByteAddressBuffer m_VB;
    ByteAddressBuffer m_IB;
    uint32_t m_IndexCount = 0;
    Math::BoundingSphere m_BoundingSphere = Math::BoundingSphere(Math::kZero);
};
