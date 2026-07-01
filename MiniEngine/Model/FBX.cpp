#include "FBX.h"
#include "../Core/Utility.h"

#include <fbxsdk.h>

using namespace DirectX;
using namespace std;

namespace FBXManager
{
	static FbxManager* s_FbxManager = nullptr;

	// 자체 싱글턴 생성
	FbxManager* GetFbxManager() 
	{
		if (s_FbxManager == nullptr)
		{
			s_FbxManager = FbxManager::Create();
			FbxIOSettings* ios = FbxIOSettings::Create(s_FbxManager, IOSROOT);
			s_FbxManager->SetIOSettings(ios);
		}

		return s_FbxManager;
	}

	// 유틸용 함수
	inline XMFLOAT4 ToQuaternion(const FbxQuaternion& q) 
	{
		return XMFLOAT4((float)q[0], (float)q[1], (float)q[2], (float)q[3]);
	}

	inline XMFLOAT3 ToFloat3(const FbxDouble3& v)
	{
		return XMFLOAT3((float)v[0], (float)v[1], (float)v[2]);
	}

	inline XMFLOAT3 ToFloat3(const FbxVector4& v)
	{
		return XMFLOAT3((float)v[0], (float)v[1], (float)v[2]);
	}

	inline XMFLOAT2 ToFloat2(const FbxVector2& v)
	{
		return XMFLOAT2((float)v[0], (float)v[1]);
	}

	// FBX 행열 기준 변환 : 열 기준 -> 행 기준
	inline XMFLOAT4X4 ToXMFloat4x4(const FbxMatrix& m) 
	{
		XMFLOAT4X4 out;

		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				out.m[r][c] = (float)m.Get(r, c);

		return out;
	}

	int FindOrAddImage(FBX::Asset& asset, const char* relPath) 
	{
		if (!relPath || relPath[0] == '\0')
			return -1;

		std::string path(relPath);

		// 기존 로드된 이미지 찾기
		for (int i = 0; i < (int)asset.m_imgs.size(); ++i)
			if (asset.m_imgs[i].path == path)
				return i;

		// 추가
		FBX::Image img;
		img.path = path;
		asset.m_imgs.push_back(std::move(img));

		return (int)asset.m_imgs.size() - 1;
	}

	std::string GetTextureRelPath(FbxProperty& prop, const FbxString& fbxDir) 
	{
		int texCount = prop.GetSrcObjectCount<FbxTexture>();
		for (int ti = 0; ti < texCount; ++ti)
		{
			FbxFileTexture* fileTex = FbxCast<FbxFileTexture>(prop.GetSrcObject<FbxTexture>(ti));
			if (!fileTex)
				continue;

			// Prefer relative name; fall back to absolute
			const char* rel = fileTex->GetRelativeFileName();
			if (rel && rel[0] != '\0')
				return std::string(rel);
			const char* abs = fileTex->GetFileName();
			if (abs && abs[0] != '\0')
				return std::string(abs);
		}

        return {};
	}

	// 재질 추출
	float ShininessToRoughness(double shininess)
	{
		if (shininess <= 1.0)
			return 1.0f;

		float r = 1.0f - (float)(log2(shininess) / 10.0);

		return max(0.0f, min(1.0f, r));
	}

	void ExtractMaterial(FBX::Asset& asset, FbxSurfaceMaterial* fbxMat)
	{
		FBX::Material mat = {};
		mat.baseColorFactor[0] = mat.baseColorFactor[1] = mat.baseColorFactor[2] = 1.0f;
		mat.baseColorFactor[3] = 1.0f;
		mat.normalTextureScale = 1.0f;
		mat.roughnessFactor = 1.0f;
		mat.baseColorTexIdx = -1;
		mat.normalTexIdx = -1;
		mat.emissiveTexIdx = -1;

		FbxString fbxDir = FbxPathUtils::GetFolderName(
			FbxString(Utility::WideStringToUTF8(asset.m_basePath + L"dummy").c_str()));

		auto* lambert = FbxCast<FbxSurfaceLambert>(fbxMat);
		if (lambert)
		{
			FbxDouble3 diffuse = lambert->Diffuse.Get();
			double factor = lambert->DiffuseFactor.Get();
			mat.baseColorFactor[0] = (float)(diffuse[0] * factor);
			mat.baseColorFactor[1] = (float)(diffuse[1] * factor);
			mat.baseColorFactor[2] = (float)(diffuse[2] * factor);
			mat.baseColorFactor[3] = 1.0f - (float)lambert->TransparencyFactor.Get();

			if (mat.baseColorFactor[3] < 1.0f)
				mat.flags |= (1 << 7); // alphaBlend

			FbxDouble3 emissive = lambert->Emissive.Get();
			double eFactor = lambert->EmissiveFactor.Get();
			mat.emissiveFactor[0] = (float)(emissive[0] * eFactor);
			mat.emissiveFactor[1] = (float)(emissive[1] * eFactor);
			mat.emissiveFactor[2] = (float)(emissive[2] * eFactor);

			// Textures
			FbxProperty diffuseProp = lambert->FindProperty(FbxSurfaceMaterial::sDiffuse);
			std::string diffusePath = GetTextureRelPath(diffuseProp, fbxDir);
			mat.baseColorTexIdx = FindOrAddImage(asset, diffusePath.c_str());

			FbxProperty normalProp = lambert->FindProperty(FbxSurfaceMaterial::sNormalMap);
			std::string normalPath = GetTextureRelPath(normalProp, fbxDir);
			mat.normalTexIdx = FindOrAddImage(asset, normalPath.c_str());

			FbxProperty emissiveProp = lambert->FindProperty(FbxSurfaceMaterial::sEmissive);
			std::string emissivePath = GetTextureRelPath(emissiveProp, fbxDir);
			mat.emissiveTexIdx = FindOrAddImage(asset, emissivePath.c_str());
		}

		auto* phong = FbxCast<FbxSurfacePhong>(fbxMat);
		if (phong)
		{
			double shininess = phong->Shininess.Get();
			mat.roughnessFactor = ShininessToRoughness(shininess);
		}

		if (!lambert)
		{
            // lambert 적용 불가 시, 처리
            mat.baseColorFactor[0] = 0.0f; 
            mat.baseColorFactor[1] = 0.0f;
            mat.baseColorFactor[2] = 0.0f;
            mat.baseColorFactor[3] = 0.0f;
		}

		asset.m_mats.push_back(mat);
	}

	// 메시 추출
    void ExtractMesh(FBX::Asset& asset, FbxMesh* fbxMesh)
    {
        FBX::Mesh mesh;
        mesh.skinIdx = -1;

        // 스킨인지 확인
        int skinCount = fbxMesh->GetDeformerCount(FbxDeformer::eSkin);
        if (skinCount > 0)
            mesh.skinIdx = (int)asset.m_skins.size();

        int polyCount = fbxMesh->GetPolygonCount();
        if (polyCount == 0)
        {
            asset.m_meshes.push_back(std::move(mesh));
            return;
        }

        // 메시에서 재질 사용 갯수 확인
        FbxNode* node = fbxMesh->GetNode();
        int matCount = node ? node->GetMaterialCount() : 0;
        if (matCount == 0) matCount = 1;

        // Per-material primitive vectors (indexed by local material slot)
        std::vector<FBX::Primitive> prims(matCount);
        for (auto& p : prims)
        {
            p.materialIdx = 0;
            p.hasSkin = skinCount > 0;
            p.minPos = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
            p.maxPos = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        }

        // Assign global material indices
        FbxGeometryElementMaterial* matElem = fbxMesh->GetElementMaterial(0);

        // Build per-control-point skin weights first
        struct SkinInfluence { uint16_t jointIdx; float weight; };
        std::vector<std::vector<SkinInfluence>> cpInfluences(fbxMesh->GetControlPointsCount());

        if (skinCount > 0)
        {
            FBX::Skin skin;
            FbxSkin* fbxSkin = (FbxSkin*)fbxMesh->GetDeformer(0, FbxDeformer::eSkin);
            int clusterCount = fbxSkin->GetClusterCount();
            skin.jointNodeIndices.resize(clusterCount);

            for (int ci = 0; ci < clusterCount; ++ci)
            {
                FbxCluster* cluster = fbxSkin->GetCluster(ci);
                FbxNode* linkNode = cluster->GetLink();
                // linearIdx assigned later; store pointer as placeholder index
                skin.jointNodeIndices[ci] = -1; // resolved in BuildFBX.cpp

                skin.jointNodeIndices[ci] = ci; // will be remapped in BuildFBX.cpp

                // IBM: the bind-pose world matrix of the joint
                FbxAMatrix transformLink;
                cluster->GetTransformLinkMatrix(transformLink);
                FbxAMatrix transform;
                cluster->GetTransformMatrix(transform);
                FbxAMatrix globalBindPoseInverseMatrix = transformLink.Inverse() * transform.Inverse();
                // Wait, actually IBM = inverse of the joint's global bind pose
                // IBM = transformLink.Inverse() is correct (transform = mesh world xform at bind)
                FbxAMatrix ibm = transformLink.Inverse();
                skin.inverseBindMatrices.push_back(ToXMFloat4x4(ibm));

                // Collect control point influences
                int* indices = cluster->GetControlPointIndices();
                double* weights = cluster->GetControlPointWeights();
                int count = cluster->GetControlPointIndicesCount();
                for (int k = 0; k < count; ++k)
                {
                    int cpIdx = indices[k];
                    if (cpIdx < (int)cpInfluences.size())
                        cpInfluences[cpIdx].push_back({ (uint16_t)ci, (float)weights[k] });
                }
            }

            asset.m_skins.push_back(std::move(skin));
        }

        // Get geometry elements
        FbxGeometryElementNormal* normalElem = fbxMesh->GetElementNormal(0);
        FbxGeometryElementTangent* tangentElem = fbxMesh->GetElementTangent(0);
        FbxGeometryElementUV* uvElem = fbxMesh->GetElementUV(0);

        FbxVector4* cpArr = fbxMesh->GetControlPoints();

        // Expand per-polygon-vertex
        for (int pi = 0; pi < polyCount; ++pi)
        {
            ASSERT(fbxMesh->GetPolygonSize(pi) == 3); // already triangulated

            // Determine material slot for this polygon
            int localMatIdx = 0;
            if (matElem)
            {
                if (matElem->GetMappingMode() == FbxGeometryElement::eByPolygon)
                    localMatIdx = matElem->GetIndexArray().GetAt(pi);
                else if (matElem->GetMappingMode() == FbxGeometryElement::eAllSame)
                    localMatIdx = matElem->GetIndexArray().GetAt(0);
            }
            localMatIdx = min(localMatIdx, matCount - 1);

            FBX::Primitive& prim = prims[localMatIdx];

            for (int vi = 0; vi < 3; ++vi)
            {
                int cpIdx = fbxMesh->GetPolygonVertex(pi, vi);
                int pvIdx = pi * 3 + vi;

                // Position
                FbxVector4 pos = cpArr[cpIdx];
                XMFLOAT3 posF((float)pos[0], (float)pos[1], (float)pos[2]);
                prim.positions.push_back(posF);

                prim.minPos.x = min(prim.minPos.x, posF.x);
                prim.minPos.y = min(prim.minPos.y, posF.y);
                prim.minPos.z = min(prim.minPos.z, posF.z);
                prim.maxPos.x = max(prim.maxPos.x, posF.x);
                prim.maxPos.y = max(prim.maxPos.y, posF.y);
                prim.maxPos.z = max(prim.maxPos.z, posF.z);

                // Normal
                if (normalElem)
                {
                    FbxVector4 n;
                    if (normalElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    {
                        int idx = normalElem->GetReferenceMode() == FbxGeometryElement::eDirect
                            ? cpIdx : normalElem->GetIndexArray().GetAt(cpIdx);
                        n = normalElem->GetDirectArray().GetAt(idx);
                    }
                    else
                    {
                        int idx = normalElem->GetReferenceMode() == FbxGeometryElement::eDirect
                            ? pvIdx : normalElem->GetIndexArray().GetAt(pvIdx);
                        n = normalElem->GetDirectArray().GetAt(idx);
                    }
                    prim.normals.push_back(XMFLOAT3((float)n[0], (float)n[1], (float)n[2]));
                }

                // Tangent
                if (tangentElem)
                {
                    FbxVector4 t;
                    if (tangentElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    {
                        int idx = tangentElem->GetReferenceMode() == FbxGeometryElement::eDirect
                            ? cpIdx : tangentElem->GetIndexArray().GetAt(cpIdx);
                        t = tangentElem->GetDirectArray().GetAt(idx);
                    }
                    else
                    {
                        int idx = tangentElem->GetReferenceMode() == FbxGeometryElement::eDirect
                            ? pvIdx : tangentElem->GetIndexArray().GetAt(pvIdx);
                        t = tangentElem->GetDirectArray().GetAt(idx);
                    }
                    prim.tangents.push_back(XMFLOAT4((float)t[0], (float)t[1], (float)t[2], (float)t[3]));
                }

                // UV
                if (uvElem)
                {
                    FbxVector2 uv;
                    if (uvElem->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    {
                        int idx = uvElem->GetReferenceMode() == FbxGeometryElement::eDirect
                            ? cpIdx : uvElem->GetIndexArray().GetAt(cpIdx);
                        uv = uvElem->GetDirectArray().GetAt(idx);
                    }
                    else
                    {
                        int idx = uvElem->GetReferenceMode() == FbxGeometryElement::eDirect
                            ? pvIdx : uvElem->GetIndexArray().GetAt(pvIdx);
                        uv = uvElem->GetDirectArray().GetAt(idx);
                    }
                    // FBX UV v is flipped vs DX convention
                    prim.uv0.push_back(XMFLOAT2((float)uv[0], 1.0f - (float)uv[1]));
                }

                // Skin weights (from control point)
                if (skinCount > 0)
                {
                    XMFLOAT4 joints = { 0, 0, 0, 0 }; // XMUINT4
                    XMFLOAT4 weights = { 0, 0, 0, 0 };

                    auto& infl = cpInfluences[cpIdx];
                    // Sort by weight descending, take top 4
                    std::sort(infl.begin(), infl.end(),
                        [](const SkinInfluence& a, const SkinInfluence& b) { return a.weight > b.weight; });

                    float totalW = 0.0f;
                    int n = min((int)infl.size(), 4);
                    for (int k = 0; k < n; ++k)
                    {
                        (&joints.x)[k] = infl[k].jointIdx;
                        (&weights.x)[k] = infl[k].weight;
                        totalW += infl[k].weight;
                    }
                    // Normalize
                    if (totalW > 0.0f)
                    {
                        float invW = 1.0f / totalW;
                        weights.x *= invW;
                        weights.y *= invW;
                        weights.z *= invW;
                        weights.w *= invW;
                    }

                    prim.jointIndices.push_back(joints);
                    prim.jointWeights.push_back(weights);
                }
            }
        }

        // Assign material indices and compute per-prim vertex counts
        for (int i = 0; i < matCount; ++i)
        {
            FBX::Primitive& prim = prims[i];
            if (prim.positions.empty())
                continue;

            prim.vertexCount = (uint32_t)prim.positions.size();

            // Map local material slot → global material index
            if (node && i < node->GetMaterialCount())
            {
                FbxSurfaceMaterial* fbxMat = node->GetMaterial(i);
                // Find this material in asset.m_materials by scanning (materials added earlier)
                // We use a parallel extraction path: store material name for lookup
                // For simplicity: material was added in order, so use scene-level index
                // We store index from the scene-level material array
                prim.materialIdx = (int)(fbxMat ? fbxMat->GetUniqueID() : 0); // resolved post-parse
            }
            else
            {
                prim.materialIdx = 0;
            }

            mesh.primitives.push_back(std::move(prim));
        }

        asset.m_meshes.push_back(std::move(mesh));
    }

    // Node 계층구조 탐색
    // 로드한 FBX 정보를 Asset에 반영
    int AddNode(FBX::Asset& asset, FbxNode* fbxNode, std::vector<std::pair<FbxNode*, int>>& nodeMap)
    {
        // 재귀 참조 안되게 주의

        for (auto& pair : nodeMap)
            if (pair.first == fbxNode)
                return pair.second;

        int idx = (int)asset.m_nodes.size();
        nodeMap.push_back({fbxNode, idx});
        asset.m_nodes.emplace_back();

        asset.m_nodes[idx].name = fbxNode->GetName();
        asset.m_nodes[idx].meshIdx = -1;
        asset.m_nodes[idx].skinIdx = -1;
        asset.m_nodes[idx].linearIdx = -1;
        asset.m_nodes[idx].skeletonRoot = false;

        // 0번 프레임에서 트랜스폼 정보 로드
        FbxAMatrix localXform = fbxNode->EvaluateLocalTransform(fbxsdk::FbxTime(0));
        FbxVector4 t = localXform.GetT();
        FbxVector4 s = localXform.GetS();
        FbxQuaternion q = localXform.GetQ();

        asset.m_nodes[idx].translation  = XMFLOAT3((float)t[0], (float)t[1], (float)t[2]);
        asset.m_nodes[idx].scale        = XMFLOAT3((float)s[0], (float)s[1], (float)s[2]);
        asset.m_nodes[idx].rotation     = XMFLOAT4((float)q[0], (float)q[1], (float)q[2], (float)q[3]);
        
        // 스켈레톤 노드 확인
        FbxNodeAttribute* attr = fbxNode->GetNodeAttribute();
        if (attr && attr->GetAttributeType() == FbxNodeAttribute::eSkeleton)
            asset.m_nodes[idx].skeletonRoot = true;

        // 메시 추가 : 스킨이라면 스킨에 추가
        FbxMesh* fbxMesh = fbxNode->GetMesh();
        if (fbxMesh)
        {
            asset.m_nodes[idx].meshIdx = (int)asset.m_meshes.size();
            ExtractMesh(asset, fbxMesh);
        }

        // 자식 노드 재귀 탐색
        for (int ci = 0; ci < fbxNode->GetChildCount(); ++ci)
        {
            int childIdx = AddNode(asset, fbxNode->GetChild(ci), nodeMap);
            asset.m_nodes[idx].children.push_back(childIdx);
        }

        return idx;
    }

    void RemapMaterialIndices(FBX::Asset& asset, FbxScene* scene) 
    {
        // Fbx 고유 id -> mat idx로 리매핑
        std::vector<std::pair<FbxLongLong, int>> idMap;
        int matCnt = scene->GetMaterialCount();
        idMap.reserve(matCnt);

        for (int i = 0; i < matCnt; ++i)
            idMap.push_back({scene->GetMaterial(i)->GetUniqueID(), i});

        for (auto& mesh : asset.m_meshes)
        {
            for (auto& prim : mesh.primitives)
            {
                FbxLongLong uid = (FbxLongLong)prim.materialIdx;
                prim.materialIdx = 0;

                for (auto& pair : idMap)
                {
                    if (pair.first == uid)
                    {
                        prim.materialIdx = pair.second;
                        break;
                    }
                }
            }
        }
    }

    // 애니메이션 샘플링
    void SampleAnimations(FBX::Asset& asset, FbxScene* scene, const std::vector<std::pair<FbxNode*, int>>& nodeMap, 
        float kFPS = 30.0f)
    {
        FbxAnimEvaluator* evaluator = scene->GetAnimationEvaluator();

        int stackCnt = scene->GetSrcObjectCount<FbxAnimStack>();
        for (int si = 0; si < stackCnt; ++si) 
        {
            FbxAnimStack* stack = scene->GetSrcObject<FbxAnimStack>(si);
            scene->SetCurrentAnimationStack(stack);

            fbxsdk::FbxTimeSpan span = stack->GetLocalTimeSpan();
            fbxsdk::FbxTime startTime = span.GetStart();
            fbxsdk::FbxTime endTime = span.GetStop();

            double durationSec = (endTime - startTime).GetSecondDouble();

            if (durationSec <= 0.0)
                continue;

            // 호환용 animStack에 저장
            FBX::AnimStack animStack;
            animStack.name = stack->GetName();
            animStack.duration = (float)durationSec;

            int frameCnt = (int)(durationSec * kFPS) + 1;

            for (auto& pair : nodeMap)
            {
                FbxNode* fbxNode = pair.first;
                int nodeIdx = pair.second;

                bool hasAnim = false;
                fbxsdk::FbxAnimLayer* layer = stack->GetMember<fbxsdk::FbxAnimLayer>(0);
                if (layer)
                {
                    hasAnim = fbxNode->LclTranslation.GetCurve(layer) != nullptr
                        || fbxNode->LclRotation.GetCurve(layer) != nullptr
                        || fbxNode->LclScaling.GetCurve(layer) != nullptr;
                }
                if (!hasAnim)
                    continue;

                // 트랜스폼 채널별로 불러오기
                FBX::AnimChannel tChan;
                tChan.targetNodeIdx = nodeIdx;
                tChan.targetPath = 0;

                FBX::AnimChannel rChan;
                rChan.targetNodeIdx = nodeIdx;
                rChan.targetPath = 1;

                FBX::AnimChannel sChan;
                sChan.targetNodeIdx = nodeIdx;
                sChan.targetPath = 2;

                // 애니메이션 전체 키프레임 로드
                for (int fi = 0; fi < frameCnt; ++fi)
                {
                    float t = (float)fi / kFPS;
                    fbxsdk::FbxTime fbxTime;
                    fbxTime.SetSecondDouble((double)startTime.GetSecondDouble() + t);

                    FbxAMatrix localXform = evaluator->GetNodeLocalTransform(fbxNode, fbxTime);
                    FbxVector4 trans = localXform.GetT();
                    FbxVector4 scale = localXform.GetS();
                    FbxQuaternion rot = localXform.GetQ();

                    FBX::AnimKey tKey;
                    tKey.time = t;
                    tKey.v[0] = (float)trans[0];
                    tKey.v[1] = (float)trans[1];
                    tKey.v[2] = (float)trans[2];
                    tKey.v[3] = 0.0f;
                    tChan.keys.push_back(tKey);

                    FBX::AnimKey rKey;
                    rKey.time = t;
                    rKey.v[0] = (float)rot[0];
                    rKey.v[1] = (float)rot[1];
                    rKey.v[2] = (float)rot[2];
                    rKey.v[3] = (float)rot[3];
                    rChan.keys.push_back(rKey);

                    FBX::AnimKey sKey;
                    sKey.v[0] = (float)scale[0];
                    sKey.v[1] = (float)scale[1];
                    sKey.v[2] = (float)scale[2];
                    sKey.v[3] = 0.0f;

                    sChan.keys.push_back(sKey);
                }
            
                if (tChan.keys.empty() == false)
                {
                    // 복사 대신 이동으로 비용 절약
                    animStack.channels.push_back(std::move(tChan));
                    animStack.channels.push_back(std::move(rChan));
                    animStack.channels.push_back(std::move(sChan));
                }
            }

            if (!animStack.channels.empty())
                asset.m_anims.push_back(std::move(animStack));
        }
    }
}

namespace FBX 
{
	bool Asset::Parse(const std::wstring& filePath) 
	{
        FbxManager* manager = FBXManager::GetFbxManager();

        FbxImporter* importer = FbxImporter::Create(manager, "");
        std::string pathUtf8 = Utility::WideStringToUTF8(filePath);

        if (!importer->Initialize(pathUtf8.c_str(), -1, manager->GetIOSettings()))
        {
            // importer 초기화 에러 출력
            Utility::Printf("[FBX: Failed to initialize importer] Path: %s\nErr: %s\n",
                pathUtf8.c_str(), importer->GetStatus().GetErrorString());

            importer->Destroy();
            return false;
        }

        FbxScene* scene = FbxScene::Create(manager, "Scene");
        if (!importer->Import(scene))
        {
            // 씬 임포트 실패
            Utility::Printf("[FBX: Failed to initialize scene] Err: %s\n",
                importer->GetStatus().GetErrorString());

            importer->Destroy();
            scene->Destroy();

            return false;
        }

        // 로드 프로세스 성공
        importer->Destroy();

        m_basePath = Utility::GetBasePath(filePath);

        // FBX 좌표계 -> direct X 좌표계 변환
        FbxAxisSystem::DirectX.ConvertScene(scene);

        // 미터(m) 단위로 변환
        FbxSystemUnit::m.ConvertScene(scene);

        //// 전체 메시 삼각화 
        FbxGeometryConverter geomConv(manager);
        geomConv.Triangulate(scene, true);

        //// 씬 순서에 따라 재질 추출
        int matCnt = scene->GetMaterialCount();
        for (int i = 0; i < matCnt; ++i)
            FBXManager::ExtractMaterial(*this, scene->GetMaterial(i));

        // 노드 계층 탐색
        FbxNode* root = scene->GetRootNode();
        std::vector<std::pair<FbxNode*, int>> nodeMap;

        if (root)
        {
            for (int ci = 0; ci < root->GetChildCount(); ++ci)
            {
                int idx = FBXManager::AddNode(*this, root->GetChild(ci), nodeMap);
                m_rootNodes.push_back(idx);
            }
        }

        FBXManager::RemapMaterialIndices(*this, scene);

        for (auto& skin : m_skins)
            (void)skin;

        FBXManager::SampleAnimations(*this, scene, nodeMap);
        scene->Destroy();

		return true;
	};

	void Shutdown()
	{
        if (FBXManager::s_FbxManager)
        {
            FBXManager::s_FbxManager->Destroy();
            FBXManager::s_FbxManager = nullptr;
        }
	}
}