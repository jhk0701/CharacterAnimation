#include "pch.h"
#include "FbxModel.h"

#include "Utility.h"

#include <vector>
#include <float.h>
#include <fbxsdk.h>

using namespace DirectX;

namespace
{
    // FBX SDK 매니저 싱글턴 (프로세스당 1개).
    FbxManager* g_FbxManager = nullptr;

    FbxManager* GetFbxManager()
    {
        if (g_FbxManager == nullptr)
        {
            g_FbxManager = FbxManager::Create();
            FbxIOSettings* ios = FbxIOSettings::Create(g_FbxManager, IOSROOT);
            g_FbxManager->SetIOSettings(ios);
        }
        return g_FbxManager;
    }

    // FBX는 행 벡터(row-vector) 규약: result_j = sum_i v_i * M(i,j) (+ 이동성분).
    XMFLOAT3 TransformPoint(const FbxAMatrix& m, const FbxVector4& v)
    {
        const double x = v[0], y = v[1], z = v[2];
        return XMFLOAT3(
            (float)(x * m.Get(0, 0) + y * m.Get(1, 0) + z * m.Get(2, 0) + m.Get(3, 0)),
            (float)(x * m.Get(0, 1) + y * m.Get(1, 1) + z * m.Get(2, 1) + m.Get(3, 1)),
            (float)(x * m.Get(0, 2) + y * m.Get(1, 2) + z * m.Get(2, 2) + m.Get(3, 2)));
    }

    // 방향(노멀)은 이동성분 제외. cube 등 강체/균등스케일에서는 상단 3x3로 충분.
    XMFLOAT3 TransformDir(const FbxAMatrix& m, const FbxVector4& v)
    {
        const double x = v[0], y = v[1], z = v[2];
        XMFLOAT3 r(
            (float)(x * m.Get(0, 0) + y * m.Get(1, 0) + z * m.Get(2, 0)),
            (float)(x * m.Get(0, 1) + y * m.Get(1, 1) + z * m.Get(2, 1)),
            (float)(x * m.Get(0, 2) + y * m.Get(1, 2) + z * m.Get(2, 2)));
        XMStoreFloat3(&r, XMVector3Normalize(XMLoadFloat3(&r)));
        return r;
    }

    // 노드의 지오메트리 오프셋(피벗)까지 포함한 최종 월드 변환.
    FbxAMatrix GetNodeWorldTransform(FbxNode* node)
    {
        FbxAMatrix world = node->EvaluateGlobalTransform();
        FbxVector4 gT = node->GetGeometricTranslation(FbxNode::eSourcePivot);
        FbxVector4 gR = node->GetGeometricRotation(FbxNode::eSourcePivot);
        FbxVector4 gS = node->GetGeometricScaling(FbxNode::eSourcePivot);
        FbxAMatrix geom(gT, gR, gS);
        return world * geom;
    }

    // 하나의 FbxMesh를 정점/인덱스로 전개(월드 공간 베이크).
    void ExtractMesh(FbxNode* node, FbxMesh* mesh,
        std::vector<FbxModel::Vertex>& outVerts,
        std::vector<uint32_t>& outIndices,
        XMFLOAT3& minPos, XMFLOAT3& maxPos)
    {
        if (mesh->GetElementNormalCount() == 0)
            mesh->GenerateNormals();

        const FbxAMatrix world = GetNodeWorldTransform(node);
        const FbxVector4* controlPoints = mesh->GetControlPoints();
        const int polyCount = mesh->GetPolygonCount();

        for (int pi = 0; pi < polyCount; ++pi)
        {
            // 삼각화된 상태를 가정 (Load에서 Triangulate 수행).
            const int polySize = mesh->GetPolygonSize(pi);
            if (polySize != 3)
                continue;

            for (int vi = 0; vi < 3; ++vi)
            {
                const int cpIdx = mesh->GetPolygonVertex(pi, vi);

                FbxModel::Vertex vtx;
                vtx.pos = TransformPoint(world, controlPoints[cpIdx]);
                vtx.pos.z = -vtx.pos.z;   // RH Y-up -> LH Y-up (DirectX)

                FbxVector4 n(0, 1, 0, 0);
                mesh->GetPolygonVertexNormal(pi, vi, n);
                vtx.normal = TransformDir(world, n);
                vtx.normal.z = -vtx.normal.z;

                minPos.x = min(minPos.x, vtx.pos.x);
                minPos.y = min(minPos.y, vtx.pos.y);
                minPos.z = min(minPos.z, vtx.pos.z);
                maxPos.x = max(maxPos.x, vtx.pos.x);
                maxPos.y = max(maxPos.y, vtx.pos.y);
                maxPos.z = max(maxPos.z, vtx.pos.z);

                outIndices.push_back((uint32_t)outVerts.size());
                outVerts.push_back(vtx);
            }
        }
    }
}

bool FbxModel::Load(const std::wstring& filePath)
{
    FbxManager* manager = GetFbxManager();

    FbxImporter* importer = FbxImporter::Create(manager, "");
    const std::string pathUtf8 = Utility::WideStringToUTF8(filePath);

    if (!importer->Initialize(pathUtf8.c_str(), -1, manager->GetIOSettings()))
    {
        Utility::Printf("[FbxModel] Failed to init importer: %s\nErr: %s\n",
            pathUtf8.c_str(), importer->GetStatus().GetErrorString());
        importer->Destroy();
        return false;
    }

    FbxScene* scene = FbxScene::Create(manager, "Scene");
    if (!importer->Import(scene))
    {
        Utility::Printf("[FbxModel] Failed to import scene: %s\n",
            importer->GetStatus().GetErrorString());
        importer->Destroy();
        scene->Destroy();
        return false;
    }
    importer->Destroy();

    // 단위만 미터로 변환(방향 불변). 축은 FbxAxisSystem::DirectX 변환이 파일에 따라 Y까지
    // 뒤집는 문제가 있어 사용하지 않고, 바인드 아래에서 Z만 반전해 RH Y-up -> LH Y-up으로 직접 변환.
    // (X Bot.fbx 원본: Y-up, 오른손 좌표계.)
    FbxSystemUnit::m.ConvertScene(scene);

    FbxGeometryConverter geomConv(manager);
    geomConv.Triangulate(scene, true);

    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    XMFLOAT3 minPos(FLT_MAX, FLT_MAX, FLT_MAX);
    XMFLOAT3 maxPos(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    // 씬 그래프 순회 (반복적 스택 탐색).
    std::vector<FbxNode*> stack;
    if (FbxNode* root = scene->GetRootNode())
    {
        for (int i = 0; i < root->GetChildCount(); ++i)
            stack.push_back(root->GetChild(i));
    }

    while (!stack.empty())
    {
        FbxNode* node = stack.back();
        stack.pop_back();

        if (FbxMesh* mesh = node->GetMesh())
            ExtractMesh(node, mesh, verts, indices, minPos, maxPos);

        for (int i = 0; i < node->GetChildCount(); ++i)
            stack.push_back(node->GetChild(i));
    }

    scene->Destroy();

    if (verts.empty() || indices.empty())
    {
        Utility::Printf("[FbxModel] No renderable geometry in %ws\n", filePath.c_str());
        return false;
    }

    // 바운딩 스피어 계산.
    XMVECTOR vMin = XMLoadFloat3(&minPos);
    XMVECTOR vMax = XMLoadFloat3(&maxPos);
    XMVECTOR vCenter = XMVectorScale(XMVectorAdd(vMin, vMax), 0.5f);
    float radius = XMVectorGetX(XMVector3Length(XMVectorSubtract(vMax, vCenter)));
    XMFLOAT3 center;
    XMStoreFloat3(&center, vCenter);
    m_BoundingSphere = Math::BoundingSphere(
        Math::Vector3(center.x, center.y, center.z), Math::Scalar(radius));

    // GPU 업로드.
    m_VB.Create(L"FBX VB", (uint32_t)verts.size(), sizeof(Vertex), verts.data());
    m_IB.Create(L"FBX IB", (uint32_t)indices.size(), sizeof(uint32_t), indices.data());
    m_IndexCount = (uint32_t)indices.size();

    Utility::Printf("[FbxModel] Loaded %ws : %u verts, %u indices\n",
        filePath.c_str(), (uint32_t)verts.size(), m_IndexCount);

    return true;
}

void FbxModel::Shutdown()
{
    if (g_FbxManager)
    {
        g_FbxManager->Destroy();
        g_FbxManager = nullptr;
    }
}
