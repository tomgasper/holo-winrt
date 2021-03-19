// Per-pixel color data passed through the pixel shader.
struct PixelShaderInput
{
    float4 pos   : SV_POSITION;
    float3 color : COLOR0;
};

cbuffer FaceColorBuffer
{
   float4 face_colors[6];
}

// The pixel shader passes through the color data. The color data from 
// is interpolated and assigned to a pixel at the rasterization step.
float4 main(PixelShaderInput input, uint tid : SV_PrimitiveID) : SV_TARGET
{
    return face_colors[(tid / 2) % 6];
}
