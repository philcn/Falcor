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
#ifndef __VK_RAYTRACING_H__
#define __VK_RAYTRACING_H__

shared RWStructuredBuffer<uint> gIndices[RT_GEOMETRY_COUNT];
shared RWStructuredBuffer<float> gTexCrds[RT_GEOMETRY_COUNT];
shared RWStructuredBuffer<float> gNormals[RT_GEOMETRY_COUNT];
shared RWStructuredBuffer<float> gBitangents[RT_GEOMETRY_COUNT];
shared RWStructuredBuffer<float> gPositions[RT_GEOMETRY_COUNT];
shared RWStructuredBuffer<float> gPrevPositions[RT_GEOMETRY_COUNT];
// VKRayTODO: Handle null view
// shared RWStructuredBuffer<float> gLightMapUVs[RT_GEOMETRY_COUNT];

shared RaytracingAccelerationStructure gRtScene;

// Replace InternalPerMeshCB
layout(shaderRecordNV) cbuffer ShaderRecord
{
    float4x4 gWorldMatLocal;              // Per-instance world transforms
    float4x4 gPrevWorldMatLocal;          // Previous frame world transforms
    float3x4 gWorldInvTransposeMatLocal;  // Per-instance matrices for transforming normals

    uint gGeometryID;
};

// If defined, hit position is computed by barycentric interpolation of the vertex positions. 
// Otherwise it is computed based on the ray equation in world space: p=o+t*d, which is numerically unstable.
// Unfortunately, interpolating the position incurs the extra cost of fetching 3x12B positions and one matrix multiply.
#define USE_INTERPOLATED_POSITION

shared cbuffer DxrPerFrame : register(b13)
{
    uint hitProgramCount;
};

float2 loadFloat2(RWStructuredBuffer<float> buffer, uint index)
{
    float2 v = 0.0;
    v[0] = buffer[index * 2];
    v[1] = buffer[index * 2 + 1];

    return v;
}

float3 loadFloat3(RWStructuredBuffer<float> buffer, uint index)
{
    float3 v = 0.0;
    v[0] = buffer[index * 3];
    v[1] = buffer[index * 3 + 1];
    v[2] = buffer[index * 3 + 2];

    return v;
}

uint3 getIndices(uint triangleIndex)
{
    uint baseIndex = triangleIndex * 3;

    uint3 indices;
    indices[0] = gIndices[NonUniformResourceIndex(gGeometryID)][baseIndex];
    indices[1] = gIndices[NonUniformResourceIndex(gGeometryID)][baseIndex + 1];
    indices[2] = gIndices[NonUniformResourceIndex(gGeometryID)][baseIndex + 2];

    return indices;
}

struct VertexOut
{
    float3 normalW    : NORMAL;
    float3 bitangentW : BITANGENT;
    float2 texC       : TEXCRD;
    float3 posW       : POSW;
    float4 colorV     : COLOR;
    float4 prevPosH   : PREVPOSH;
    float2 lightmapC  : LIGHTMAPUV;
    float4 posH : SV_POSITION;
};

VertexOut getVertexAttributes(uint triangleIndex, float3 barycentrics)
{
    uint3 indices = getIndices(triangleIndex);
    VertexOut v;
    v.texC = 0;
    v.normalW = 0;
    v.bitangentW = 0;
#ifdef USE_INTERPOLATED_POSITION
    v.posW = 0;
#else
    v.posW = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
#endif
    v.colorV = 0;
    v.prevPosH = 0;
    v.lightmapC = 0;

    [unroll]
    for (int i = 0; i < 3; i++)
    {
        int address = (indices[i] * 3) * 4;
        v.texC       += loadFloat2(gTexCrds[NonUniformResourceIndex(gGeometryID)], indices[i])     * barycentrics[i];
        v.normalW    += loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[i])     * barycentrics[i];
        v.bitangentW += loadFloat3(gBitangents[NonUniformResourceIndex(gGeometryID)], indices[i])  * barycentrics[i];
// VKRayTODO: Handle null view
//        v.lightmapC  += loadFloat2(gLightMapUVs[NonUniformResourceIndex(gGeometryID)], indices[i]) * barycentrics[i];
#ifdef USE_INTERPOLATED_POSITION
        v.posW       += loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[i])   * barycentrics[i];
#endif
    }
#ifdef USE_INTERPOLATED_POSITION
    v.posW = mul(float4(v.posW, 1.f), gWorldMatLocal).xyz;
#endif
#ifndef _MS_DISABLE_INSTANCE_TRANSFORM
    // Transform normal/bitangent to world space
    v.normalW = mul(v.normalW, (float3x3)gWorldInvTransposeMatLocal).xyz;
    v.bitangentW = mul(v.bitangentW, (float3x3)gWorldMatLocal).xyz;
#endif
    v.normalW = normalize(v.normalW);
    v.bitangentW = normalize(v.bitangentW);
    return v;
}

VertexOut getVertexAttributes(uint triangleIndex, BuiltInTriangleIntersectionAttributes attribs)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    return getVertexAttributes(triangleIndex, barycentrics);
}

float3 getGeoNormal(const float3 e[2])
{
    return normalize(cross(e[0], e[1]));
}

/** Returns a triangle's vertex normals and edges in object space.
    \param[in] triangleIndex Index of the triangle.
    \param[out] n Vertex normals in object space.
    \param[out] e Triangle edges in object space.
*/
void getTriNormalsAndEdgesInObjectSpace(uint triangleIndex, out float3 n[3], out float3 e[2])
{
    uint3 indices = getIndices(triangleIndex);

    float3 p[3];
    p[0] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[0]);
    p[1] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[1]);
    p[2] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[2]);

    e[0] = p[1] - p[0];
    e[1] = p[2] - p[0];

    n[0] = loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[0]);
    n[1] = loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[1]);
    n[2] = loadFloat3(gNormals[NonUniformResourceIndex(gGeometryID)], indices[2]);
}

/** Returns a triangle's geometric normal in world space.
    \param[in] triangleIndex Index of the triangle.
    \return Geometric normal in world space. Front facing for counter-clockwise winding.
*/
float3 getGeometricNormalW(uint triangleIndex)
{
    uint3 indices = getIndices(triangleIndex);

    float3 p[3];
    p[0] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[0]);
    p[1] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[1]);
    p[2] = loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[2]);

    float3 e[2];
    e[0] = p[1] - p[0];
    e[1] = p[2] - p[0];

    float3 N = getGeoNormal(e);
    return mul(N, (float3x3)gWorldInvTransposeMatLocal).xyz;
}

/** Returns position on triangle in the previous frame in world space.
*/
float3 getPrevPosW(uint triangleIndex, float3 barycentrics)
{
    uint3 indices = getIndices(triangleIndex);
    float3 prevPos = float3(0, 0, 0);

    [unroll]
    for (int i = 0; i < 3; i++)
    {
        // Load vertex in object space from vertex buffer for previous frame if it exists, otherwise from the current frame.
        prevPos += loadFloat3(gPositions[NonUniformResourceIndex(gGeometryID)], indices[i]) * barycentrics[i];
    }

    return mul(float4(prevPos, 1.f), gPrevWorldMatLocal).xyz;
}

float3 getPrevPosW(uint triangleIndex, BuiltInTriangleIntersectionAttributes attribs)
{
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    return getPrevPosW(triangleIndex, barycentrics);
}

#endif // __VK_RAYTRACING_H__