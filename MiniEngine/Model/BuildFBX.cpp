#include "ModelLoader.h"
#include "Renderer.h"
#include "FBX.h"
#include "TextureConvert.h"
#include "MeshConvert.h"
#include "Model.h"
#include "IndexOptimizePostTransform.h"
#include "Animation.h"
#include "../Core/Utility.h"
#include "../Core/Math/Common.h"

#include "DirectXMesh.h"

#include <map>
#include <algorithm>

using namespace DirectX;
using namespace Math;
using namespace Renderer;
using namespace Graphics;

static void OptimizeFbxPrimitive(
    Renderer::Primitive& outPrim,
    const FBX::Primitive& inPrim,
    const Matrix4& localToObject,
    bool alphaBlend,
    bool alphaTest,
    bool twoSided)
{
    const uint32_t vertexCount = inPrim.vertexCount;
    ASSERT(vertexCount > 0 && vertexCount % 3 == 0,
        "FBX primitive must be triangulated (poly-vertex expanded)");

    // Generate sequential index buffer (already expanded per-polygon-vertex)
    const uint32_t indexCount = vertexCount;
    const bool b32BitIndices = (vertexCount - 1) > 0xFFFF;
    const uint32_t indexSize = b32BitIndices ? 4 : 2;

    outPrim.IB = std::make_shared<std::vector<byte>>(indexSize * indexCount);

    if (b32BitIndices)
    {
        uint32_t* ib = (uint32_t*)outPrim.IB->data();
        for (uint32_t i = 0; i < indexCount; ++i) ib[i] = i;
        OptimizeFaces(ib, indexCount, ib, 64);
    }
    else
    {
        uint16_t* ib = (uint16_t*)outPrim.IB->data();
        for (uint16_t i = 0; i < (uint16_t)indexCount; ++i) ib[i] = i;
        OptimizeFaces(ib, indexCount, ib, 64);
    }

    void* indices = outPrim.IB->data();

    // 바운딩 볼륨
    {
        Vector3 minLS(inPrim.minPos.x, inPrim.minPos.y, inPrim.minPos.z);
        Vector3 maxLS(inPrim.maxPos.x, inPrim.maxPos.y, inPrim.maxPos.z);
        Vector3 centerLS = (minLS + maxLS) * 0.5f;
        Scalar maxRadiusLSSq(kZero);
        Scalar maxRadiusOSSq(kZero);

        Vector3 centerOS = Vector3(localToObject * Vector4(centerLS));

        outPrim.m_BBoxLS = AxisAlignedBox(kZero);
        outPrim.m_BBoxOS = AxisAlignedBox(kZero);

        for (uint32_t v = 0; v < vertexCount; ++v)
        {
            Vector3 posLS(inPrim.positions[v].x, inPrim.positions[v].y, inPrim.positions[v].z);
            maxRadiusLSSq = Max(maxRadiusLSSq, LengthSquare(centerLS - posLS));
            outPrim.m_BBoxLS.AddPoint(posLS);

            Vector3 posOS = Vector3(localToObject * Vector4(posLS));
            maxRadiusOSSq = Max(maxRadiusOSSq, LengthSquare(centerOS - posOS));
            outPrim.m_BBoxOS.AddPoint(posOS);
        }

        outPrim.m_BoundsLS = Math::BoundingSphere(centerLS, Sqrt(maxRadiusLSSq));
        outPrim.m_BoundsOS = Math::BoundingSphere(centerOS, Sqrt(maxRadiusOSSq));
        ASSERT(outPrim.m_BoundsOS.GetRadius() > 0.0f);
    }

    // -----------------------------------------------------------------------
    // Normal / tangent generation
    // -----------------------------------------------------------------------
    const uint32_t faceCount = indexCount / 3;

    std::unique_ptr<XMFLOAT3[]> normals;
    std::unique_ptr<XMFLOAT4[]> tangents;

    if (inPrim.normals.empty())
    {
        normals.reset(new XMFLOAT3[vertexCount]);
        if (b32BitIndices)
            ComputeNormals((const uint32_t*)indices, faceCount, inPrim.positions.data(), vertexCount, CNORM_DEFAULT, normals.get());
        else
            ComputeNormals((const uint16_t*)indices, faceCount, inPrim.positions.data(), vertexCount, CNORM_DEFAULT, normals.get());
    }

    const XMFLOAT3* normalsPtr = inPrim.normals.empty() ? normals.get() : inPrim.normals.data();

    if (inPrim.tangents.empty() && !inPrim.uv0.empty())
    {
        tangents.reset(new XMFLOAT4[vertexCount]);
        HRESULT hr;
        if (b32BitIndices)
            hr = ComputeTangentFrame((const uint32_t*)indices, faceCount, inPrim.positions.data(), normalsPtr, inPrim.uv0.data(), vertexCount, tangents.get());
        else
            hr = ComputeTangentFrame((const uint16_t*)indices, faceCount, inPrim.positions.data(), normalsPtr, inPrim.uv0.data(), vertexCount, tangents.get());
        ASSERT_SUCCEEDED(hr, "Failed to generate tangent frame");
    }

    const XMFLOAT4* tangentsPtr = inPrim.tangents.empty() ? tangents.get() : inPrim.tangents.data();

    // -----------------------------------------------------------------------
    // PSO flags
    // -----------------------------------------------------------------------
    outPrim.psoFlags = PSOFlags::kHasPosition | PSOFlags::kHasNormal;

    if (tangentsPtr)
        outPrim.psoFlags |= PSOFlags::kHasTangent;
    if (!inPrim.uv0.empty())
        outPrim.psoFlags |= PSOFlags::kHasUV0;
    if (inPrim.hasSkin)
        outPrim.psoFlags |= PSOFlags::kHasSkin;
    if (alphaBlend)
        outPrim.psoFlags |= PSOFlags::kAlphaBlend;
    if (alphaTest)
        outPrim.psoFlags |= PSOFlags::kAlphaTest;
    if (twoSided)
        outPrim.psoFlags |= PSOFlags::kTwoSided;

    // -----------------------------------------------------------------------
    // Build compressed vertex buffer
    // -----------------------------------------------------------------------
    std::vector<D3D12_INPUT_ELEMENT_DESC> outputElements;
    outputElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    outputElements.push_back({ "NORMAL",   0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    if (tangentsPtr)
        outputElements.push_back({ "TANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    if (!inPrim.uv0.empty())
        outputElements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    if (inPrim.hasSkin)
    {
        outputElements.push_back({ "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,  0, D3D12_APPEND_ALIGNED_ELEMENT });
        outputElements.push_back({ "BLENDWEIGHT",  0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    }

    D3D12_INPUT_LAYOUT_DESC layout = { outputElements.data(), (uint32_t)outputElements.size() };

    VBWriter vbw;
    vbw.Initialize(layout);

    uint32_t offsets[10];
    uint32_t strides[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    ComputeInputLayout(layout, offsets, strides);
    uint32_t stride = strides[0];

    outPrim.VB = std::make_shared<std::vector<byte>>(stride * vertexCount);
    ASSERT_SUCCEEDED(vbw.AddStream(outPrim.VB->data(), vertexCount, 0, stride));

    vbw.Write(inPrim.positions.data(), "POSITION", 0, vertexCount);
    vbw.Write(normalsPtr, "NORMAL", 0, vertexCount, true);
    if (tangentsPtr)
        vbw.Write(tangentsPtr, "TANGENT", 0, vertexCount, true);
    if (!inPrim.uv0.empty())
        vbw.Write(inPrim.uv0.data(), "TEXCOORD", 0, vertexCount);
    if (inPrim.hasSkin && !inPrim.jointIndices.empty())
    {
        vbw.Write(inPrim.jointIndices.data(), "BLENDINDICES", 0, vertexCount);
        vbw.Write(inPrim.jointWeights.data(), "BLENDWEIGHT", 0, vertexCount);
    }

    // Depth-only VB
    uint32_t depthStride = 12;
    std::vector<D3D12_INPUT_ELEMENT_DESC> depthElements;
    depthElements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    if (alphaTest && !inPrim.uv0.empty())
    {
        depthStride += 4;
        depthElements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    }
    if (inPrim.hasSkin)
    {
        depthStride += 16;
        depthElements.push_back({ "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT,  0, D3D12_APPEND_ALIGNED_ELEMENT });
        depthElements.push_back({ "BLENDWEIGHT",  0, DXGI_FORMAT_R16G16B16A16_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT });
    }

    VBWriter dvbw;
    dvbw.Initialize({ depthElements.data(), (uint32_t)depthElements.size() });

    outPrim.DepthVB = std::make_shared<std::vector<byte>>(depthStride * vertexCount);
    ASSERT_SUCCEEDED(dvbw.AddStream(outPrim.DepthVB->data(), vertexCount, 0, depthStride));

    dvbw.Write(inPrim.positions.data(), "POSITION", 0, vertexCount);
    if (alphaTest && !inPrim.uv0.empty())
        dvbw.Write(inPrim.uv0.data(), "TEXCOORD", 0, vertexCount);
    if (inPrim.hasSkin && !inPrim.jointIndices.empty())
    {
        dvbw.Write(inPrim.jointIndices.data(), "BLENDINDICES", 0, vertexCount);
        dvbw.Write(inPrim.jointWeights.data(), "BLENDWEIGHT", 0, vertexCount);
    }

    outPrim.vertexStride = (uint16_t)stride;
    outPrim.index32 = b32BitIndices ? 1 : 0;
    outPrim.materialIdx = inPrim.materialIdx;
    outPrim.primCount = indexCount;
}



static void CompileFbxMesh(
    std::vector<Mesh*>& meshList,
    std::vector<byte>& bufferMemory,
    const FBX::Mesh& srcMesh,
    const std::vector<FBX::Material>& materials,
    uint32_t matrixIdx,
    const Matrix4& localToObject,
    Math::BoundingSphere& boundingSphere,
    AxisAlignedBox& boundingBox)
{
    Math::BoundingSphere sphereOS(kZero);
    AxisAlignedBox bboxOS(kZero);

    std::vector<Renderer::Primitive> primitives(srcMesh.primitives.size());

    for (uint32_t i = 0; i < (uint32_t)srcMesh.primitives.size(); ++i)
    {
        const FBX::Primitive& inPrim = srcMesh.primitives[i];
        if (inPrim.vertexCount == 0)
            continue;

        bool alphaBlend = false, alphaTest = false, twoSided = false;
        if (inPrim.materialIdx >= 0 && inPrim.materialIdx < (int)materials.size())
        {
            uint32_t flags = materials[inPrim.materialIdx].flags;
            alphaBlend = (flags & (1 << 7)) != 0;
            alphaTest = (flags & (1 << 6)) != 0;
            twoSided = (flags & (1 << 5)) != 0;
        }

        OptimizeFbxPrimitive(primitives[i], inPrim, localToObject, alphaBlend, alphaTest, twoSided);
        sphereOS = sphereOS.Union(primitives[i].m_BoundsOS);
        bboxOS.AddBoundingBox(primitives[i].m_BBoxOS);
    }

    boundingSphere = sphereOS;
    boundingBox = bboxOS;

    size_t totalVertexSize = 0, totalDepthVertexSize = 0, totalIndexSize = 0;
    std::map<uint32_t, std::vector<Renderer::Primitive*>> renderMeshes;

    for (auto& prim : primitives)
    {
        if (!prim.VB) continue;
        renderMeshes[prim.hash].push_back(&prim);
        totalVertexSize += prim.VB->size();
        totalDepthVertexSize += prim.DepthVB->size();
        totalIndexSize += Math::AlignUp(prim.IB->size(), 4);
    }

    if (renderMeshes.empty())
        return;

    uint32_t totalBufferSize = (uint32_t)(totalVertexSize + totalDepthVertexSize + totalIndexSize);
    Utility::ByteArray stagingBuffer;
    stagingBuffer.reset(new std::vector<byte>(totalBufferSize));
    uint8_t* uploadMem = stagingBuffer->data();

    uint32_t curVBOffset = 0;
    uint32_t curDepthVBOffset = (uint32_t)totalVertexSize;
    uint32_t curIBOffset = curDepthVBOffset + (uint32_t)totalDepthVertexSize;

    for (auto& iter : renderMeshes)
    {
        size_t numDraws = iter.second.size();
        Mesh* mesh = (Mesh*)malloc(sizeof(Mesh) + sizeof(Mesh::Draw) * (numDraws - 1));

        size_t vbSize = 0, vbDepthSize = 0, ibSize = 0;
        Math::BoundingSphere collectiveSphere(kZero);

        for (auto& draw : iter.second)
        {
            vbSize += draw->VB->size();
            vbDepthSize += draw->DepthVB->size();
            ibSize += draw->IB->size();
            collectiveSphere = collectiveSphere.Union(draw->m_BoundsLS);
        }

        mesh->bounds[0] = collectiveSphere.GetCenter().GetX();
        mesh->bounds[1] = collectiveSphere.GetCenter().GetY();
        mesh->bounds[2] = collectiveSphere.GetCenter().GetZ();
        mesh->bounds[3] = collectiveSphere.GetRadius();
        mesh->vbOffset = (uint32_t)bufferMemory.size() + curVBOffset;
        mesh->vbSize = (uint32_t)vbSize;
        mesh->vbDepthOffset = (uint32_t)bufferMemory.size() + curDepthVBOffset;
        mesh->vbDepthSize = (uint32_t)vbDepthSize;
        mesh->ibOffset = (uint32_t)bufferMemory.size() + curIBOffset;
        mesh->ibSize = (uint32_t)ibSize;
        mesh->vbStride = (uint8_t)iter.second[0]->vertexStride;
        mesh->ibFormat = uint8_t(iter.second[0]->index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT);
        mesh->meshCBV = (uint16_t)matrixIdx;
        mesh->materialCBV = iter.second[0]->materialIdx;
        mesh->psoFlags = iter.second[0]->psoFlags;
        mesh->pso = 0xFFFF;

        if (srcMesh.skinIdx >= 0)
        {
            mesh->numJoints = 0xFFFF; // resolved in BuildFbxSkins
            mesh->startJoint = (uint16_t)srcMesh.skinIdx;
        }
        else
        {
            mesh->numJoints = 0;
            mesh->startJoint = 0xFFFF;
        }

        mesh->numDraws = (uint16_t)numDraws;

        uint32_t drawIdx = 0;
        uint32_t curVertOffset = 0, curIndexOffset = 0;
        for (auto& draw : iter.second)
        {
            Mesh::Draw& d = mesh->draw[drawIdx++];
            d.primCount = draw->primCount;
            d.baseVertex = curVertOffset;
            d.startIndex = curIndexOffset;
            std::memcpy(uploadMem + curVBOffset + curVertOffset, draw->VB->data(), draw->VB->size());
            curVertOffset += (uint32_t)draw->VB->size() / draw->vertexStride;
            std::memcpy(uploadMem + curDepthVBOffset, draw->DepthVB->data(), draw->DepthVB->size());
            std::memcpy(uploadMem + curIBOffset + curIndexOffset, draw->IB->data(), draw->IB->size());
            curIndexOffset += (uint32_t)draw->IB->size() >> (draw->index32 + 1);
        }

        curVBOffset += (uint32_t)vbSize;
        curDepthVBOffset += (uint32_t)vbDepthSize;
        curIBOffset += (uint32_t)Math::AlignUp(ibSize, 4);

        meshList.push_back(mesh);
    }

    bufferMemory.insert(bufferMemory.end(), stagingBuffer->begin(), stagingBuffer->end());
}


static uint32_t WalkFbxGraph(
    ModelData& model,
    const FBX::Asset& asset,
    const std::vector<int>& siblings,
    uint32_t curPos,
    const Matrix4& xform)
{
    for (int nodeIdx : siblings)
    {
        const FBX::Node& srcNode = asset.m_nodes[nodeIdx];
        GraphNode& graphNode = model.m_SceneGraph[curPos];

        graphNode.hasChildren = 0;
        graphNode.hasSibling = 0;
        graphNode.matrixIdx = curPos;
        graphNode.skeletonRoot = srcNode.skeletonRoot ? 1 : 0;

        // Record linearIdx for animation and skin resolution
        const_cast<FBX::Node&>(srcNode).linearIdx = (int)curPos;

        XMFLOAT3 s = srcNode.scale;
        XMFLOAT4 r = srcNode.rotation;
        XMFLOAT3 t = srcNode.translation;

        graphNode.scale = XMFLOAT3(s.x, s.y, s.z);
        graphNode.rotation = Quaternion(Vector4(r.x, r.y, r.z, r.w));

        Matrix3 rotScale = Matrix3(graphNode.rotation) * Matrix3::MakeScale(graphNode.scale);
        graphNode.xform = Matrix4(rotScale, Vector3(t.x, t.y, t.z));

        const Matrix4 LocalXform = xform * graphNode.xform;

        if (srcNode.meshIdx >= 0 && srcNode.meshIdx < (int)asset.m_meshes.size())
        {
            Math::BoundingSphere sphereOS;
            AxisAlignedBox boxOS;
            CompileFbxMesh(model.m_Meshes, model.m_GeometryData,
                asset.m_meshes[srcNode.meshIdx], asset.m_mats,
                curPos, LocalXform, sphereOS, boxOS);
            model.m_BoundingSphere = model.m_BoundingSphere.Union(sphereOS);
            model.m_BoundingBox.AddBoundingBox(boxOS);
        }

        uint32_t nextPos = curPos + 1;

        if (!srcNode.children.empty())
        {
            graphNode.hasChildren = 1;
            nextPos = WalkFbxGraph(model, asset, srcNode.children, nextPos, LocalXform);
        }

        if (&nodeIdx != &siblings.back())
            graphNode.hasSibling = 1;

        curPos = nextPos;
    }
    return curPos;
}

static void BuildFbxMaterials(ModelData& model, const FBX::Asset& asset)
{
    const uint32_t numMaterials = (uint32_t)asset.m_mats.size();
    model.m_MaterialConstants.resize(numMaterials);
    model.m_MaterialTextures.resize(numMaterials);

    // Register texture names
    model.m_TextureNames.resize(asset.m_imgs.size());
    model.m_TextureOptions.resize(asset.m_imgs.size(), 0xFF);

    for (size_t i = 0; i < asset.m_imgs.size(); ++i)
        model.m_TextureNames[i] = asset.m_imgs[i].path; // Utility::UTF8ToWideString(asset.m_imgs[i].path);

    // Trigger DDS conversion for each referenced texture
    for (size_t i = 0; i < asset.m_imgs.size(); ++i)
    {
        std::wstring fullPath = asset.m_basePath + Utility::UTF8ToWideString(model.m_TextureNames[i]); // asset.m_basePath + model.m_TextureNames[i];
        CompileTextureOnDemand(fullPath, model.m_TextureOptions[i]);
    }

    for (uint32_t mi = 0; mi < numMaterials; ++mi)
    {
        const FBX::Material& src = asset.m_mats[mi];
        MaterialConstantData& dst = model.m_MaterialConstants[mi];

        dst.baseColorFactor[0] = src.baseColorFactor[0];
        dst.baseColorFactor[1] = src.baseColorFactor[1];
        dst.baseColorFactor[2] = src.baseColorFactor[2];
        dst.baseColorFactor[3] = src.baseColorFactor[3];
        dst.emissiveFactor[0] = src.emissiveFactor[0];
        dst.emissiveFactor[1] = src.emissiveFactor[1];
        dst.emissiveFactor[2] = src.emissiveFactor[2];
        dst.normalTextureScale = src.normalTextureScale;
        dst.metallicFactor = src.metallicFactor;
        dst.roughnessFactor = src.roughnessFactor;
        dst.flags = src.flags;

        MaterialTextureData& texData = model.m_MaterialTextures[mi];
        texData.addressModes = 0;

        // kNumTextures = 5: BaseColor, MetallicRoughness, Occlusion, Emissive, Normal
        for (uint32_t ti = 0; ti < kNumTextures; ++ti)
            texData.stringIdx[ti] = 0xFFFF;

        if (src.baseColorTexIdx >= 0)
        {
            texData.stringIdx[kBaseColor] = (uint16_t)src.baseColorTexIdx;
            model.m_TextureOptions[src.baseColorTexIdx] = TextureOptions(true, false);
        }
        if (src.normalTexIdx >= 0)
        {
            texData.stringIdx[kNormal] = (uint16_t)src.normalTexIdx;
            model.m_TextureOptions[src.normalTexIdx] = TextureOptions(false);
        }
        if (src.emissiveTexIdx >= 0)
        {
            texData.stringIdx[kEmissive] = (uint16_t)src.emissiveTexIdx;
            model.m_TextureOptions[src.emissiveTexIdx] = TextureOptions(true);
        }

        // All FBX textures wrap by default (TEXTURE_ADDRESS_WRAP = 1)
        for (uint32_t ti = 0; ti < kNumTextures; ++ti)
            texData.addressModes |= 0x5 << (ti * 4);
    }
}

static void BuildFbxSkins(ModelData& model, const FBX::Asset& asset)
{
    if (asset.m_skins.empty())
        return;

    std::vector<std::pair<uint16_t, uint16_t>> skinMap; // offset, count
    skinMap.reserve(asset.m_skins.size());

    for (const FBX::Skin& skin : asset.m_skins)
    {
        uint16_t numJoints = (uint16_t)skin.jointNodeIndices.size();
        uint16_t curOffset = (uint16_t)model.m_JointIndices.size();
        skinMap.push_back({ curOffset, numJoints });

        for (int jointIdx : skin.jointNodeIndices)
        {
            // jointIdx here is the cluster index; we need the linearIdx of the joint node.
            // During WalkFbxGraph, linearIdx was assigned. Look up the FBX::Node whose
            // name matches, then use its linearIdx.
            // As a fallback (if skin joint indices weren't resolved), use 0.
            uint16_t linearIdx = 0;
            if (jointIdx >= 0 && jointIdx < (int)asset.m_nodes.size())
                linearIdx = (uint16_t)std::max(0, asset.m_nodes[jointIdx].linearIdx);
            model.m_JointIndices.push_back(linearIdx);
        }

        for (const XMFLOAT4X4& ibm : skin.inverseBindMatrices)
        {
            XMMATRIX xmMat = DirectX::XMLoadFloat4x4(&ibm);
            model.m_JointIBMs.push_back(Matrix4(xmMat)); // Matrix4(*(const XMFLOAT4X4*)&ibm)
        }
    }

    // Assign resolved joint offset/count to skinned meshes
    for (Mesh* mesh : model.m_Meshes)
    {
        if (mesh->numJoints != 0) // 0xFFFF = skinned, needs resolution
        {
            uint16_t skinIdx = mesh->startJoint;
            if (skinIdx < (uint16_t)skinMap.size())
            {
                mesh->startJoint = skinMap[skinIdx].first;
                mesh->numJoints = skinMap[skinIdx].second;
            }
        }
    }
}


static void BuildFbxAnimations(ModelData& model, const FBX::Asset& asset)
{
    if (asset.m_anims.empty())
        return;

    model.m_Animations.resize(asset.m_anims.size());

    for (uint32_t ai = 0; ai < (uint32_t)asset.m_anims.size(); ++ai)
    {
        const FBX::AnimStack& srcAnim = asset.m_anims[ai];
        AnimationSet& dstAnim = model.m_Animations[ai];

        dstAnim.duration = srcAnim.duration;
        dstAnim.firstCurve = (uint32_t)model.m_AnimationCurves.size();
        dstAnim.numCurves = (uint32_t)srcAnim.channels.size();

        for (const FBX::AnimChannel& chan : srcAnim.channels)
        {
            if (chan.keys.empty())
            {
                --dstAnim.numCurves;
                continue;
            }

            AnimationCurve curve = {};
            curve.targetNode = (uint32_t)chan.targetNodeIdx;
            curve.targetPath = (uint32_t)chan.targetPath;
            curve.interpolation = AnimationCurve::kLinear;
            curve.keyFrameFormat = AnimationCurve::kFloat;
            // Stride in 4-byte words: translation/scale = 3 floats, rotation = 4 floats
            curve.keyFrameStride = (chan.targetPath == 1 /*kRotation*/) ? 4 : 3;
            curve.keyFrameOffset = (uint32_t)model.m_AnimationKeyFrameData.size();
            curve.numSegments = (float)(chan.keys.size() - 1);

            float startTime = chan.keys.front().time;
            float endTime = chan.keys.back().time;
            curve.startTime = startTime;
            curve.rangeScale = (endTime > startTime)
                ? curve.numSegments / (endTime - startTime) : 1.0f;

            // Append keyframe float data
            for (const FBX::AnimKey& key : chan.keys)
            {
                uint32_t numFloats = curve.keyFrameStride;
                const byte* src = (const byte*)key.v;
                model.m_AnimationKeyFrameData.insert(
                    model.m_AnimationKeyFrameData.end(),
                    src, src + numFloats * 4);
            }

            model.m_AnimationCurves.push_back(curve);
        }
    }
}

bool Renderer::BuildModel(ModelData& model, const FBX::Asset& asset)
{
    BuildFbxMaterials(model, asset);

    model.m_SceneGraph.resize(asset.m_nodes.size());
    model.m_BoundingSphere = Math::BoundingSphere(kZero);
    model.m_BoundingBox = AxisAlignedBox(kZero);

    uint32_t numNodes = WalkFbxGraph(model, asset, asset.m_rootNodes, 0, Matrix4(kIdentity));
    model.m_SceneGraph.resize(numNodes);

    BuildFbxSkins(model, asset);
    BuildFbxAnimations(model, asset);

    return true;
}
