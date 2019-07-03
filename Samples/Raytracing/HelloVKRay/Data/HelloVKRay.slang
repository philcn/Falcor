shared RaytracingAccelerationStructure gRtScene;

shared layout(rgba8) RWTexture2D<float4> gOutput;

struct RayPayload
{
    float3 hitValue;
};

layout(shaderRecordNV)
cbuffer ShaderRecord
{
    float4x4 pad;
    float4 constColor;
}

#define gl_LaunchIDNV DispatchRaysIndex()
#define gl_LaunchSizeNV DispatchRaysDimensions()

void rayGen() 
{
    const float2 pixelCenter = float2(gl_LaunchIDNV.xy) + float2(0.5);
    const float2 inUV = pixelCenter / float2(gl_LaunchSizeNV.xy);

    float2 d = inUV * 2.0 - 1.0;
    float aspectRatio = float(gl_LaunchSizeNV.x) / float(gl_LaunchSizeNV.y);

    RayDesc ray;
    ray.Origin = float3(0, 0, -2.0);
    ray.TMin = 0.001;
    ray.Direction = normalize(float3(d.x * aspectRatio, -d.y, 1));
    ray.TMax = 100.0;

    RayPayload payload;

    TraceRay
    (
        gRtScene,
        0, // ray flags; gl_RayFlagsOpaqueNV
        0xff, // cull mask
        0, // sbt record offset
        0, // sbt record stride
        0, // missIndex
        ray,
        payload
    );

    gOutput[int2(gl_LaunchIDNV.xy)] = float4(payload.hitValue, 1.0);
}

void closestHit(BuiltInTriangleIntersectionAttributes attribs, in out RayPayload payload)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    payload.hitValue = constColor.rgb;// barycentrics;
}

void miss(in out RayPayload payload)
{
    payload.hitValue = float3(0.0, 0.1, 0.3);
}
