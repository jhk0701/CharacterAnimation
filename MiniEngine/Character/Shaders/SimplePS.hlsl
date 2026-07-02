cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    float3   SunDirection;
    float    pad0;
    float3   BaseColor;     
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

    float3 color = BaseColor * (0.55 + 0.45 * ndl);

    return float4(color, 1.0);
}
