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
#include "Experimental/Raytracing/RtScene.h"
#include "API/Device.h"

namespace Falcor
{
    VkDeviceMemory allocateDeviceMemory(Device::MemoryType memType, uint32_t memoryTypeBits, size_t size);
    VkMemoryRequirements getAccelerationStructureMemoryRequirements(VkAccelerationStructureNV handle, VkAccelerationStructureMemoryRequirementsTypeNV type);

    // VKRayTODO: share common logic with D3D12 implementation
    std::vector<RtScene::VkGeometryInstance> RtScene::createInstanceDesc(const RtScene* pScene, uint32_t hitProgCount)
    {
        mGeometryCount = 0;
        std::vector<VkGeometryInstance> instanceDesc;
        mModelInstanceData.resize(pScene->getModelCount());

        uint32_t tlasIndex = 0;
        uint32_t instanceContributionToHitGroupIndex = 0;
        // Loop over all the models
        for (uint32_t modelId = 0; modelId < pScene->getModelCount(); modelId++)
        {
            auto& modelInstanceData = mModelInstanceData[modelId];
            const RtModel* pModel = dynamic_cast<RtModel*>(pScene->getModel(modelId).get());
            assert(pModel); // Can't work on regular models
            modelInstanceData.modelBase = tlasIndex;
            modelInstanceData.meshInstancesPerModelInstance = 0;
            modelInstanceData.meshBase.resize(pModel->getMeshCount());

            for (uint32_t modelInstance = 0; modelInstance < pScene->getModelInstanceCount(modelId); modelInstance++)
            {
                const auto& pModelInstance = pScene->getModelInstance(modelId, modelInstance);
                // Loop over the meshes
                for (uint32_t blasId = 0; blasId < pModel->getBottomLevelDataCount(); blasId++)
                {
                    // Initialize the instance desc
                    const auto& blasData = pModel->getBottomLevelData(blasId);
                    VkGeometryInstance idesc = {};
                    uint64_t blasHandle;
                    vk_call(vkGetAccelerationStructureHandleNV(gpDevice->getApiHandle(), blasData.pBlas, sizeof(uint64_t), &blasHandle));
                    idesc.accelerationStructureHandle = blasHandle;

                    // Set the meshes tlas offset
                    if (modelInstance == 0)
                    {
                        for (uint32_t i = 0; i < blasData.meshCount; i++)
                        {
                            assert(blasData.meshCount == 1 || pModel->getMeshInstanceCount(blasData.meshBaseIndex + i) == 1);   // A BLAS shouldn't have multiple instanced meshes
                            modelInstanceData.meshBase[blasData.meshBaseIndex + i] = modelInstanceData.meshInstancesPerModelInstance + i;   // If i>0 each mesh has a single instance
                        }
                    }

                    uint32_t meshInstanceCount = pModel->getMeshInstanceCount(blasData.meshBaseIndex);
                    for (uint32_t meshInstance = 0; meshInstance < meshInstanceCount; meshInstance++)
                    {
                        idesc.instanceId = uint32_t(instanceDesc.size());
                        idesc.mask = 0xff;
                        idesc.instanceOffset = 0;
                        idesc.flags = 0;
                        idesc.instanceOffset = instanceContributionToHitGroupIndex;
                        instanceContributionToHitGroupIndex += hitProgCount * blasData.meshCount;

                        const auto& pMaterial = pModel->getMeshInstance(blasData.meshBaseIndex, meshInstance)->getObject()->getMaterial();

                        // TODO: This code is incorrect since a BLAS can have multiple meshes with different materials and hence different doubleSided flags.
                        if (pMaterial->isDoubleSided())
                        {
                            idesc.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
                        }

                        // Only apply mesh-instance transform on non-skinned meshes
                        mat4 transform = pModelInstance->getTransformMatrix();
                        if (blasData.isStatic)
                        {
                            transform = transform * pModel->getMeshInstance(blasData.meshBaseIndex, meshInstance)->getTransformMatrix();    // If there are multiple meshes in a BLAS, they all have the same transform
                        }
                        transform = transpose(transform);
                        memcpy(idesc.transform, &transform, sizeof(idesc.transform));
                        instanceDesc.push_back(idesc);
                        mGeometryCount += blasData.meshCount;
                        if (modelInstance == 0) modelInstanceData.meshInstancesPerModelInstance += blasData.meshCount;
                        tlasIndex += blasData.meshCount;
                        assert(tlasIndex * hitProgCount == instanceContributionToHitGroupIndex);
                    }
                }
            }
        }
        assert(tlasIndex == mGeometryCount);

        // Validate that our getInstanceId() helper returns contigous indices.
        uint32_t instanceId = 0;
        for (uint32_t model = 0; model < getModelCount(); model++)
        {
            for (uint32_t modelInstance = 0; modelInstance < getModelInstanceCount(model); modelInstance++)
            {
                for (uint32_t mesh = 0; mesh < getModel(model)->getMeshCount(); mesh++)
                {
                    for (uint32_t meshInstance = 0; meshInstance < getModel(model)->getMeshInstanceCount(mesh); meshInstance++)
                    {
                        assert(getInstanceId(model, modelInstance, mesh, meshInstance) == instanceId++);
                    }
                }
            }
        }
        assert(instanceId == mGeometryCount);

        return instanceDesc;
    }

    // TODO: Cache TLAS per hitProgCount, as some render pipelines need multiple TLAS:es with different #hit progs in same frame, currently that trigger rebuild every frame. See issue #365.
    void RtScene::createTlas(uint32_t hitProgCount)
    {
        if (mTlasHitProgCount == hitProgCount) return;
        mTlasHitProgCount = hitProgCount;

        // Early out if hit program count is zero or if scene is empty.
        if (hitProgCount == 0 || getModelCount() == 0)
        {
            mModelInstanceData.clear();
            mpTopLevelAS = nullptr;
            mGeometryCount = 0;
            mInstanceCount = 0;
            mRefit = false;
            return;
        }

        // todo: move this somewhere fair.
        mRtFlags |= RtBuildFlags::AllowUpdate;

        VkBuildAccelerationStructureFlagsNV vkRayFlags = getVKRayBuildFlags(mRtFlags);

        RenderContext* pContext = gpDevice->getRenderContext();
        std::vector<VkGeometryInstance> instanceDesc = createInstanceDesc(this, hitProgCount);

        // todo: improve this check - make sure things have not changed much and update was enabled last time
        bool isRefitPossible = mRefit && mpTopLevelAS && (mInstanceCount == (uint32_t)instanceDesc.size());

        mInstanceCount = (uint32_t)instanceDesc.size();
        
        if (!isRefitPossible)
        {
            // Create the top-level acceleration buffers
            VkAccelerationStructureCreateInfoNV accelerationStructureInfo;
            accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
            accelerationStructureInfo.pNext = nullptr;
            accelerationStructureInfo.compactedSize = 0;
            accelerationStructureInfo.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
            accelerationStructureInfo.info.pNext = NULL;
            accelerationStructureInfo.info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            accelerationStructureInfo.info.flags = 0;
            accelerationStructureInfo.info.instanceCount = mInstanceCount;
            accelerationStructureInfo.info.geometryCount = 0;
            accelerationStructureInfo.info.pGeometries = nullptr;

            VkAccelerationStructureNV as;
            vk_call(vkCreateAccelerationStructureNV(gpDevice->getApiHandle(), &accelerationStructureInfo, nullptr, &as));
            mpTopLevelAS = AccelerationStructureHandle::create(as);

            // Bind acceleration structure memory
            VkMemoryRequirements reqs = getAccelerationStructureMemoryRequirements(mpTopLevelAS, VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV);
            VkDeviceMemory memory = allocateDeviceMemory(Device::MemoryType::Default, reqs.memoryTypeBits, reqs.size);

            VkBindAccelerationStructureMemoryInfoNV bindInfo;
            bindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
            bindInfo.pNext = nullptr;
            bindInfo.accelerationStructure = mpTopLevelAS;
            bindInfo.memory = memory;
            bindInfo.memoryOffset = 0;
            bindInfo.deviceIndexCount = 0;
            bindInfo.pDeviceIndices = nullptr;

            vk_call(vkBindAccelerationStructureMemoryNV(gpDevice->getApiHandle(), 1, &bindInfo));
        }
        else
        {
            // VKRayTODO: insert barrier
            // pContext->uavBarrier(mpTopLevelAS.get());
        }

        // Create instance buffer
        uint32_t instanceBufferSize = mInstanceCount * (uint32_t)sizeof(VkGeometryInstance);
        Buffer::SharedPtr pInstanceData = Buffer::create(instanceBufferSize, Resource::BindFlags::RayTracing, Buffer::CpuAccess::None, instanceDesc.data());

        // Create scratch buffer
        VkAccelerationStructureMemoryRequirementsTypeNV type = isRefitPossible ? VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV : VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
        VkDeviceSize scratchBufferSize = getAccelerationStructureMemoryRequirements(mpTopLevelAS, type).size;
        Buffer::SharedPtr pScratchBuffer = Buffer::create(scratchBufferSize, Resource::BindFlags::RayTracing, Buffer::CpuAccess::None);

        assert((mInstanceCount != 0) && pInstanceData->getApiHandle() && mpTopLevelAS && pScratchBuffer->getApiHandle());

        // Build acceleration structure
        VkAccelerationStructureInfoNV asInfo;
        asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
        asInfo.pNext = nullptr;
        asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
        asInfo.flags = 0;
        asInfo.instanceCount = mInstanceCount;
        asInfo.geometryCount = 0;
        asInfo.pGeometries = nullptr;

        if (!isRefitPossible)
        {
            vkCmdBuildAccelerationStructureNV(pContext->getLowLevelData()->getCommandList(), &asInfo, pInstanceData->getApiHandle(), 0, VK_FALSE, mpTopLevelAS, VK_NULL_HANDLE, pScratchBuffer->getApiHandle(), 0);
        }
        else
        {
            vkCmdBuildAccelerationStructureNV(pContext->getLowLevelData()->getCommandList(), &asInfo, pInstanceData->getApiHandle(), 0, VK_TRUE, mpTopLevelAS, mpTopLevelAS, pScratchBuffer->getApiHandle(), 0);
        }

        pContext->accelerationStructureBarrier();

        mRefit = false;
    }
}
