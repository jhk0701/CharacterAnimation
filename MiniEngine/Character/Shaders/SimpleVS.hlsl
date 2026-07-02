
cbuffer Constants : register(b0)
{
    float4x4 ViewProj;
    float3   SunDirection;
    float    pad0;
    float3   BaseColor;
    float    pad1;
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
