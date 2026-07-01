#pragma once

#include <string>
#include <vector>

#include <DirectXMath.h>

using namespace DirectX;

namespace FBX
{
	struct Image 
	{
		std::string path;
	};

	struct Material
	{
        float baseColorFactor[4];    
        float emissiveFactor[3];     
        float normalTextureScale;    // Always 1.0 (FBX has no scale)
        float metallicFactor;        
        float roughnessFactor;       
        uint32_t flags;              // PSOFlags bits : alphaTest, alphaBlend, twoSided
        int baseColorTexIdx;         // -1 = none
        int normalTexIdx;
        int emissiveTexIdx;
	};

#pragma region 버텍스 및 메시 데이터

    // Triangle 데이터 확장 -> 각 폴리곤 버텍스 정보 구조체
    struct Primitive
    {
        std::vector<XMFLOAT3> positions;
        std::vector<XMFLOAT3> normals;
        std::vector<XMFLOAT4> tangents;
        std::vector<XMFLOAT2> uv0;
        std::vector<XMFLOAT4>  jointIndices; // XMUINT4->XMFLOAT4
        std::vector<XMFLOAT4> jointWeights;
        uint32_t vertexCount;
        int materialIdx;
        uint32_t attribMask;
        bool hasSkin;
        XMFLOAT3 minPos;
        XMFLOAT3 maxPos;
    };

    struct Mesh
    {
        std::vector<Primitive> primitives; // 버텍스 정보
        int skinIdx; // -1 : 스킨 없음
    };

    // FBX SDK 로드 호환용 Node
    struct Node
    {
        std::string name;
        int meshIdx;
        int skinIdx;
        std::vector<int> children;
        int linearIdx;
        bool skeletonRoot;

        XMFLOAT3 scale;
        XMFLOAT3 translation;
        XMFLOAT4 rotation; // 쿼터니언 회전값
    };

    // 스키닝 데이터
    struct Skin
    {
        std::vector<int> jointNodeIndices;
        std::vector<XMFLOAT4X4> inverseBindMatrices;
    };

#pragma endregion

#pragma region 애니메이션

    struct AnimKey
    {
        float time;
        float v[4]; // xyz : translation / scale, xyzw : rotation(quat)
    };

    struct AnimChannel
    {
        int targetNodeIdx;
        int targetPath;
        std::vector<AnimKey> keys;
    };

    struct AnimStack
    {
        std::string name;
        float duration;

        // 채널 구성
        // 0 : translation
        // 1 : rotation
        // 2 : scale
        std::vector<AnimChannel> channels;
    };

#pragma endregion

    // 로드된 모델 클래스
    class Asset 
    {
    public:
        Asset() {};
        Asset(const std::wstring& filePath) { Parse(filePath); }
    
        bool Parse(const std::wstring& filePath);

        std::wstring m_basePath;
        std::vector<Image> m_imgs;
        std::vector<Material> m_mats;
        std::vector<Mesh> m_meshes;
        std::vector<Node> m_nodes;
        std::vector<Skin> m_skins;
        std::vector<AnimStack> m_anims;
        std::vector<int> m_rootNodes;
    };

    // FBX SDK : FbxManager* 리소스 반환용
    void Shutdown();
}