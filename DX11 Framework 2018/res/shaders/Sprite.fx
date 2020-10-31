#pragma pack_matrix( row_major )

cbuffer ObjectBuffer : register( b0 )
{
    float4x4 wvpMatrix;
}

struct VS_INPUT
{
    float3 inPosition : POSITION;
    float2 inTexCoord : TEXCOORD;
};

struct VS_OUPUT
{
    float4 outPosition : SV_POSITION;
    float2 outTexCoord : TEXCOORD;
};

VS_OUPUT VS( VS_INPUT input )
{
    VS_OUPUT output;
    output.outPosition = mul( float4( input.inPosition, 1.0f ), wvpMatrix );
    output.outTexCoord = input.inTexCoord;
    return output;
}

// pixel shader
struct PS_INPUT
{
	float4 inPosition : SV_POSITION;
    float2 inTexCoord : TEXCOORD;
};

Texture2D objTexture : TEXTURE : register( t0 );
SamplerState samplerState : SAMPLER : register( s0 );

float4 PS( PS_INPUT input ) : SV_TARGET
{
    return objTexture.Sample( samplerState, input.inTexCoord );
}