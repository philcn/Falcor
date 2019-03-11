struct ReflectionRay
{
    float4 color;
};

struct ShadowRay
{
    float hitDistance;
};

struct SphereHitAttributes
{
    float3 normal;
};

StructuredBuffer<float4> colors;

layout(shaderRecordNV) cbuffer ShaderRecord
{
    uint shaderRecordID;
}

void miss(in out ShadowRay ray)
{
	ray.hitDistance = 10000.0f;
}

void closestHit(BuiltInTriangleIntersectionAttributes attributes, in out ReflectionRay ioPayload)
{
    uint materialID = InstanceIndex() + InstanceID() + PrimitiveIndex() + HitKind() + shaderRecordID;

    float4 color = colors[materialID];
    color *= RayTCurrent() - RayTMin();

    ioPayload.color = color;
}

void anyHit(SphereHitAttributes attributes, in out ShadowRay ioPayload)
{
    AcceptHitAndEndSearch();
}

layout(rgba8);
RWTexture2D<float4> outputImage;

RaytracingAccelerationStructure as;

#define gl_LaunchIDNV DispatchRaysIndex()
#define gl_LaunchSizeNV DispatchRaysDimensions()

void raygen() 
{
    float2 inUV = float2
    (
        (float(gl_LaunchIDNV.x) + 0.5f) / float(gl_LaunchSizeNV.x),
        (float(gl_LaunchIDNV.y) + 0.5f) / float(gl_LaunchSizeNV.y)
    );

    RayDesc ray;
    ray.Origin = float3(0, 0, -2.0);
    ray.TMin = 0.1;
    ray.Direction = float3(0, 0, 0);
    ray.TMax = 100000;

    ShadowRay shadowRay;
    shadowRay.hitDistance = 0;

    TraceRay
    (
        as,
        1, // ray flags
        0xff, // cull mask
        0, // sbt record offset
        0, // sbt record stride
        0, // missIndex
        ray,
        shadowRay
    );
}
