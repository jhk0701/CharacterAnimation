// Character 자체 FBX 렌더 경로용 최소 픽셀 셰이더.
// 간단한 N·L 방향광 조명. HDR 선형 값 출력 -> 프레임워크의 PostEffects가 톤매핑.

cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    float3   SunDirection;
    float    pad0;
    float3   BaseColor;     // diffuse (흰색 = 선명한 렌더)
    float    pad1;
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

    // 앰비언트를 배경(0.39)보다 높여 흰색 캐릭터가 선명히 드러나도록 함.
    float3 color = BaseColor * (0.55 + 0.45 * ndl);

    return float4(color, 1.0);
}
