// Character 자체 FBX 렌더 경로용 최소 정점 셰이더.
// Model 프로젝트 파이프라인과 무관하게 position+normal 정적 메시를 렌더한다.

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;      // world->clip (정점은 이미 월드 공간으로 베이크됨)
    float3   SunDirection;
    float    pad;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
};

VSOutput main(VSInput vsInput)
{
    VSOutput vsOutput;
    vsOutput.position = mul(ViewProj, float4(vsInput.position, 1.0));
    vsOutput.normal   = vsInput.normal;
    return vsOutput;
}
