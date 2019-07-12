/***************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************/

// Must be in global root signature; Vulkan raytracing doesn't support SRV in shader record
shared RWTexture2D<float4> gOutput;

shared RaytracingAccelerationStructure gRtScene;

// shared Buffer<uint3> gIndices[]; // VKRayTODO: Should be using unbounded array of Buffer

// Temp workaround
// Using StructuredBuffer will return all 0's
// Using uint3 and float3 will result in wrong indexing caused by alignment
shared RWStructuredBuffer<uint> gIndices[7];
shared RWStructuredBuffer<float> gPositions[7];
shared RWStructuredBuffer<float> gNormals[7];

shared cbuffer PerFrameCB
{
    float4x4 invView;
    float4x4 invModel;
    float2 viewportDims;
    float tanHalfFovY;
};

struct PrimaryRayData
{
    float3 color;
};

layout(shaderRecordNV) cbuffer ShaderRecord
{
    float4x4 pad; // Test ShaderRecord size calculation and alignment
    float3 gInstanceColor;
    uint gGeometryID;
}

void rayGen() 
{
    uint3 launchIndex = DispatchRaysIndex();
    float2 d = (((launchIndex.xy + 0.5) / viewportDims) * 2.f - 1.f);
    float aspectRatio = viewportDims.x / viewportDims.y;

    RayDesc ray;
    ray.Origin = invView[3].xyz;
        
    // We negate the Z exis because the 'view' matrix is generated using a 
    // Right Handed coordinate system with Z pointing towards the viewer
    // The negation of Z axis is needed to get the rays go out in the direction away fromt he viewer.
    // The negation of Y axis is needed because the texel coordinate system, used in the UAV we write into using launchIndex
    // has the Y axis flipped with respect to the camera Y axis (0 is at the top and 1 at the bottom)
    ray.Direction = normalize( (d.x * invView[0].xyz * tanHalfFovY * aspectRatio) - (d.y * invView[1].xyz * tanHalfFovY) - invView[2].xyz );

    ray.TMin = 0;
    ray.TMax = 100000;

    PrimaryRayData payload;
    TraceRay( gRtScene, 0 /* ray flags */, 0xFF, 0 /* ray index */, 1 /* geometry contribution */, 0 /* miss index */, ray, payload );
    gOutput[launchIndex.xy] = float4(payload.color, 1.0);
}

float3 loadFloat3(RWStructuredBuffer<float> buffer, uint index)
{
    float3 v = 0.0;
    v[0] = buffer[index * 3];
    v[1] = buffer[index * 3 + 1];
    v[2] = buffer[index * 3 + 2];

    return v;
}

void closestHit(BuiltInTriangleIntersectionAttributes attribs, in out PrimaryRayData payload)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);

    uint indices[3];
    indices[0] = gIndices[NonUniformResourceIndex(gGeometryID)][PrimitiveIndex() * 3];
    indices[1] = gIndices[NonUniformResourceIndex(gGeometryID)][PrimitiveIndex() * 3 + 1];
    indices[2] = gIndices[NonUniformResourceIndex(gGeometryID)][PrimitiveIndex() * 3 + 2];

    float3 p[3];
    p[0] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[0]);
    p[1] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[1]);
    p[2] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[2]);

    float3 n[3];
    n[0] = loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[0]);
    n[1] = loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[1]);
    n[2] = loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[2]);

    float3 position = 0.0;
    float3 normal = 0.0;

    for (int i = 0; i < 3; ++i)
    {
        position += p[i] * barycentrics[i];
        normal += n[i] * barycentrics[i];
    }

    // payload.color = float3(indices) / 1000.0f;
    // payload.color = position / 100.0f;
    payload.color = normal * 0.5 + 0.5;

    // payload.color = gInstanceColor.rgb;
    // payload.color = barycentrics;
}

void miss(in out PrimaryRayData payload)
{
    payload.color = float3(0.0, 0.1, 0.3);
}
