// Character 자체 FBX 렌더 경로용 최소 픽셀 셰이더.
// 간단한 N·L 방향광 조명. HDR 선형 값 출력 -> 프레임워크의 PostEffects가 톤매핑.

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    float3   SunDirection;
    float    pad;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
};

float4 main(PSInput psInput) : SV_TARGET
{
    float3 n   = normalize(psInput.normal);
    float  ndl = saturate(dot(n, normalize(-SunDirection)));

    float3 baseColor = float3(0.8, 0.8, 0.8);
    float3 color = baseColor * (0.2 + 0.8 * ndl);   // 앰비언트 0.2 + 디퓨즈

    return float4(color, 1.0);
}
