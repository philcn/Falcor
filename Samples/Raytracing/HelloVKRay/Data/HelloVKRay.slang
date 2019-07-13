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
__import Raytracing;

// Must be in global root signature since Vulkan raytracing doesn't support SRV in shader record
shared RWTexture2D<float4> gOutput;

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

void closestHit(BuiltInTriangleIntersectionAttributes attribs, in out PrimaryRayData payload)
{
    uint triangleIndex = PrimitiveIndex();
    float3 rayOrigW = WorldRayOrigin();

    VertexOut v = getVertexAttributes(triangleIndex, attribs);
    ShadingData sd = rtPrepareShadingData(v, rayOrigW);
    
    payload.color = sd.diffuse;
}

void miss(in out PrimaryRayData payload)
{
    payload.color = float3(0.0, 0.1, 0.3);
}
