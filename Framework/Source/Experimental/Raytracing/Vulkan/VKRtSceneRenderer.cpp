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
#include "VKRtSceneRenderer.h"
#include "Experimental/Raytracing/RtProgramVars.h"
#include "Experimental/Raytracing/RtState.h"

namespace Falcor
{
    VKRtSceneRenderer::SharedPtr VKRtSceneRenderer::create(RtScene::SharedPtr pScene)
    {
        return SharedPtr(new VKRtSceneRenderer(pScene));
    }

    static bool setVertexBuffer(ParameterBlockReflection::BindLocation bindLocation, uint32_t vertexLoc, const Vao* pVao, GraphicsVars* pVars, uint32_t geometryID)
    {
        if (bindLocation.setIndex != ProgramReflection::kInvalidLocation)
        {
            const auto& elemDesc = pVao->getElementIndexByLocation(vertexLoc);
            if (elemDesc.elementIndex == Vao::ElementDesc::kInvalidIndex)
            {
                pVars->getDefaultBlock()->setSrv(bindLocation, geometryID, nullptr);
                pVars->getDefaultBlock()->setUav(bindLocation, geometryID, nullptr); // VKRayTODO: Temp
            }
            else
            {
                assert(elemDesc.elementIndex == 0);
                pVars->getDefaultBlock()->setSrv(bindLocation, geometryID, pVao->getVertexBuffer(elemDesc.vbIndex)->getSRV());
                return true;
            }
        }
        return false;
    }

    void VKRtSceneRenderer::bindMeshBuffers(const Vao* pVao, GraphicsVars* pVars, uint32_t geometryID)
    {
        if (mMeshBufferLocations.indices.setIndex != ProgramReflection::kInvalidLocation)
        {
            auto pSrv = pVao->getIndexBuffer() ? pVao->getIndexBuffer()->getSRV() : nullptr;
            pVars->getDefaultBlock()->setSrv(mMeshBufferLocations.indices, geometryID, pSrv);
        }

// VKRayTODO
//        setVertexBuffer(mMeshBufferLocations.lightmapUVs, VERTEX_LIGHTMAP_UV_LOC, pVao, pVars, geometryID);
        setVertexBuffer(mMeshBufferLocations.texC, VERTEX_TEXCOORD_LOC, pVao, pVars, geometryID);
        setVertexBuffer(mMeshBufferLocations.normal, VERTEX_NORMAL_LOC, pVao, pVars, geometryID);
        setVertexBuffer(mMeshBufferLocations.position, VERTEX_POSITION_LOC, pVao, pVars, geometryID);
        setVertexBuffer(mMeshBufferLocations.bitangent, VERTEX_BITANGENT_LOC, pVao, pVars, geometryID);

        // Bind vertex buffer for previous positions if it exists. If not, we bind the current positions.
        if (!setVertexBuffer(mMeshBufferLocations.prevPosition, VERTEX_PREV_POSITION_LOC, pVao, pVars, geometryID))
        {
            setVertexBuffer(mMeshBufferLocations.prevPosition, VERTEX_POSITION_LOC, pVao, pVars, geometryID);
        }
    }

    void VKRtSceneRenderer::setHitShaderData(RtProgramVars* pRtVars, InstanceData& data)
    {
        const RtScene* pScene = dynamic_cast<RtScene*>(mpScene.get());
        uint32_t instanceId = pScene->getInstanceId(data.model, data.modelInstance, data.mesh, data.meshInstance);
        data.currentData.pVars = pRtVars->getHitVars(data.progId)[instanceId].get();
        if(data.currentData.pVars)
        {
            const Model* pModel = mpScene->getModel(data.model).get();
            const Scene::ModelInstance* pModelInstance = mpScene->getModelInstance(data.model, data.modelInstance).get();
            const Mesh* pMesh = pModel->getMesh(data.mesh).get();
            const Model::MeshInstance* pMeshInstance = pModel->getMeshInstance(data.mesh, data.meshInstance).get();

            assert(!pMesh->hasBones()); // Skinning not supported
            glm::mat4 worldMat = pModelInstance->getTransformMatrix() * pMeshInstance->getTransformMatrix();
            glm::mat4 prevWorldMat = pModelInstance->getPrevTransformMatrix() * pMeshInstance->getTransformMatrix();
            glm::mat3x4 worldInvTransposeMat = transpose(inverse(glm::mat3(worldMat)));

            // Populate shader records
            auto pCB = data.currentData.pVars->getDefaultBlock()->getConstantBuffer("ShaderRecord");
            pCB["gWorldMatLocal"] = worldMat;
            pCB["gPrevWorldMatLocal"] = prevWorldMat;
            pCB["gWorldInvTransposeMatLocal"] = worldInvTransposeMat;
            pCB["gGeometryID"] = instanceId;

            // Actually binding on global buffer
            bindMeshBuffers(pMesh->getVao().get(), pRtVars->getGlobalVars().get(), instanceId);

            // VKRayTODO: Bind material
            // setPerMaterialData(data.currentData, pMesh->getMaterial().get());
        }
    }

    void VKRtSceneRenderer::initializeMeshBufferLocation(const ProgramReflection* pReflection)
    {
        mMeshBufferLocations.indices = pReflection->getDefaultParameterBlock()->getResourceBinding("gIndices");
        mMeshBufferLocations.texC = pReflection->getDefaultParameterBlock()->getResourceBinding("gTexCrds");
        mMeshBufferLocations.lightmapUVs = pReflection->getDefaultParameterBlock()->getResourceBinding("gLightMapUVs");
        mMeshBufferLocations.normal = pReflection->getDefaultParameterBlock()->getResourceBinding("gNormals");
        mMeshBufferLocations.position = pReflection->getDefaultParameterBlock()->getResourceBinding("gPositions");
        mMeshBufferLocations.prevPosition = pReflection->getDefaultParameterBlock()->getResourceBinding("gPrevPositions");
        mMeshBufferLocations.bitangent = pReflection->getDefaultParameterBlock()->getResourceBinding("gBitangents");
    }

    bool VKRtSceneRenderer::setPerMeshInstanceData(const CurrentWorkingData& currentData, const Scene::ModelInstance* pModelInstance, const Model::MeshInstance* pMeshInstance, uint32_t drawInstanceID)
    {
        return SceneRenderer::setPerMeshInstanceData(currentData, pModelInstance, pMeshInstance, drawInstanceID);
    }

    void VKRtSceneRenderer::setPerFrameData(RtProgramVars* pRtVars, InstanceData& data)
    {
        SceneRenderer::setPerFrameData(data.currentData);
    }

    void VKRtSceneRenderer::setRayGenShaderData(RtProgramVars* pRtVars, InstanceData& data)
    {
    }

    void VKRtSceneRenderer::setGlobalData(RtProgramVars* pRtVars, InstanceData& data)
    {
        data.currentData.pVars = pRtVars->getGlobalVars().get();

        GraphicsVars* pVars = data.currentData.pVars;
        ParameterBlockReflection::BindLocation loc = pVars->getReflection()->getDefaultParameterBlock()->getResourceBinding("gRtScene");
        if (loc.setIndex != ProgramReflection::kInvalidLocation)
        {
            RtScene* pRtScene = dynamic_cast<RtScene*>(mpScene.get());
            pVars->getDefaultBlock()->setAccelerationStructure(loc, 0, pRtScene->getTlas(pRtVars->getHitProgramsCount()));
        }

        ConstantBuffer::SharedPtr pDxrPerFrame = pVars->getConstantBuffer("DxrPerFrame");
        if (pDxrPerFrame)
        {
            pDxrPerFrame["hitProgramCount"] = pRtVars->getHitProgramsCount();
        }
        SceneRenderer::setPerFrameData(data.currentData);
    }

    void VKRtSceneRenderer::setMissShaderData(RtProgramVars* pRtVars, InstanceData& data)
    {
    }

    void VKRtSceneRenderer::renderScene(RenderContext* pContext, RtProgramVars::SharedPtr pRtVars, RtState::SharedPtr pState, uvec2 targetDim, Camera* pCamera)
    {
        renderScene(pContext, pRtVars, pState, uvec3(targetDim, 1), pCamera);
    }

    void VKRtSceneRenderer::renderScene(RenderContext* pContext, RtProgramVars::SharedPtr pRtVars, RtState::SharedPtr pState, uvec3 targetDim, Camera* pCamera)
    {
        InstanceData data;
        data.currentData.pCamera = pCamera == nullptr ? mpScene->getActiveCamera().get() : pCamera;
        uint32_t hitCount = pRtVars->getHitProgramsCount();
        if (hitCount)
        {   
            updateVariableOffsets(pState->getProgram()->getHitProgram(0)->getGlobalReflector().get(), true /* global only */);
            initializeMeshBufferLocation(pState->getProgram()->getGlobalReflector().get());
        }

        setRayGenShaderData(pRtVars.get(), data);
        setGlobalData(pRtVars.get(), data);

        // Set the miss-shader data
        for (data.progId = 0; data.progId < pRtVars->getMissProgramsCount(); data.progId++)
        {
            if(pRtVars->getMissVars(data.progId))
            {
                setMissShaderData(pRtVars.get(), data);
            }
        }

        // Set the hit-shader data
        for(data.progId = 0 ; data.progId < hitCount ; data.progId++)
        {
            if(pRtVars->getHitVars(data.progId).empty()) continue;
            for (data.model = 0; data.model < mpScene->getModelCount(); data.model++)
            {
                const Model* pModel = mpScene->getModel(data.model).get();
                data.currentData.pModel = pModel;
                for (data.modelInstance = 0; data.modelInstance < mpScene->getModelInstanceCount(data.model); data.modelInstance++)
                {
                    for (data.mesh = 0; data.mesh < pModel->getMeshCount(); data.mesh++)
                    {
                        for (data.meshInstance = 0; data.meshInstance < pModel->getMeshInstanceCount(data.mesh); data.meshInstance++)
                        {
                            setHitShaderData(pRtVars.get(), data);
                        }
                    }
                }
            }
        }

        if (!pRtVars->apply(pContext, pState->getRtso().get()))
        {
            logError("VKRtSceneRenderer::renderScene() - applying RtProgramVars failed, most likely because we ran out of descriptors.", true);
            assert(false);
        }
        pContext->raytrace(pRtVars, pState, targetDim.x, targetDim.y, targetDim.z);
    }
}
