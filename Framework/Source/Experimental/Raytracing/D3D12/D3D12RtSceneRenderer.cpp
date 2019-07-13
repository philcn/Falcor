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
#include "Framework.h"
#include "../RtSceneRenderer.h"
#include "../RtProgramVars.h"

namespace Falcor
{
    static bool setVertexBuffer(ParameterBlockReflection::BindLocation bindLocation, uint32_t vertexLoc, const Vao* pVao, GraphicsVars* pVars)
    {
        if (bindLocation.setIndex != ProgramReflection::kInvalidLocation)
        {
            const auto& elemDesc = pVao->getElementIndexByLocation(vertexLoc);
            if (elemDesc.elementIndex == Vao::ElementDesc::kInvalidIndex)
            {
                pVars->getDefaultBlock()->setSrv(bindLocation, 0, nullptr);
            }
            else
            {
                assert(elemDesc.elementIndex == 0);
                pVars->getDefaultBlock()->setSrv(bindLocation, 0, pVao->getVertexBuffer(elemDesc.vbIndex)->getSRV());
                return true;
            }
        }
        return false;
    }

    void RtSceneRenderer::bindMeshBuffers(const Vao * pVao, GraphicsVars * pVars, uint32_t geometryID /* unused */)
    {
        if (mMeshBufferLocations.indices.setIndex != ProgramReflection::kInvalidLocation)
        {
            auto pSrv = pVao->getIndexBuffer() ? pVao->getIndexBuffer()->getSRV() : nullptr;
            pVars->getDefaultBlock()->setSrv(mMeshBufferLocations.indices, 0, pSrv);
        }

        setVertexBuffer(mMeshBufferLocations.lightmapUVs, VERTEX_LIGHTMAP_UV_LOC, pVao, pVars);
        setVertexBuffer(mMeshBufferLocations.texC, VERTEX_TEXCOORD_LOC, pVao, pVars);
        setVertexBuffer(mMeshBufferLocations.normal, VERTEX_NORMAL_LOC, pVao, pVars);
        setVertexBuffer(mMeshBufferLocations.position, VERTEX_POSITION_LOC, pVao, pVars);
        setVertexBuffer(mMeshBufferLocations.bitangent, VERTEX_BITANGENT_LOC, pVao, pVars);

        // Bind vertex buffer for previous positions if it exists. If not, we bind the current positions.
        if (!setVertexBuffer(mMeshBufferLocations.prevPosition, VERTEX_PREV_POSITION_LOC, pVao, pVars))
        {
            setVertexBuffer(mMeshBufferLocations.prevPosition, VERTEX_POSITION_LOC, pVao, pVars);
        }
    }

    bool RtSceneRenderer::setPerModelData(const CurrentWorkingData& currentData)
    {
        return SceneRenderer::setPerModelData(currentData);
    }

    bool RtSceneRenderer::setPerMeshInstanceData(const CurrentWorkingData& currentData, const Scene::ModelInstance* pModelInstance, const Model::MeshInstance* pMeshInstance, uint32_t drawInstanceID)
    {
        return SceneRenderer::setPerMeshInstanceData(currentData, pModelInstance, pMeshInstance, drawInstanceID);
    }
}
