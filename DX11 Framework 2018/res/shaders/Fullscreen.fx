// Vertex Shader
struct VS_INPUT
{
    float2 inPos : POSITION;
};

struct VS_OUTPUT
{
    float2 outTex : TEXCOORD;
    float4 outPos : SV_POSITION;
};

VS_OUTPUT VS( VS_INPUT input )
{   
    VS_OUTPUT output;
    output.outPos = float4( input.inPos, 0.0f, 1.0f );
    input.inPos = float2( ( input.inPos.x + 1 ) / 2.0f, -( input.inPos.y - 1 ) / 2.0f );
    //input.inPos.x = ( input.inPos.x + 1 );
    //input.inPos.y = -( input.inPos.y - 1 );
    output.outTex = input.inPos;
    return output;
}

// Pixel Shader
struct PS_INPUT
{
    float2 inTex : TEXCOORD;
    float4 inPos : SV_POSITION;
};

Texture2D quadTexture : TEXTURE : register( t0 );
SamplerState samplerState : SAMPLER : register( s0 );

float4 PS( PS_INPUT input ) : SV_TARGET
{
    return quadTexture.Sample( samplerState, input.inTex ).rgba;
}