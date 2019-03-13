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
#include "API/Device.h"
#include "Experimental/Raytracing/RtProgramVars.h"
#include "Experimental/Raytracing/RtStateObject.h"
#include "VkRtVarsCmdList.h"

namespace Falcor
{
    uint32_t RtProgramVars::getProgramIdentifierSize()
    {
        static uint32_t sShaderGroupHandleSize = ~0;
        if (sShaderGroupHandleSize == ~0)
        {
            VkPhysicalDeviceRayTracingPropertiesNV rayTracingProperties = {};
            rayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
            rayTracingProperties.pNext = nullptr;
            rayTracingProperties.maxRecursionDepth = 0;
            rayTracingProperties.shaderGroupHandleSize = 0;

            VkPhysicalDeviceProperties2 props;
            props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props.pNext = &rayTracingProperties;
            props.properties = { };
            vkGetPhysicalDeviceProperties2(gpDevice->getApiHandle(), &props);

            sShaderGroupHandleSize = rayTracingProperties.shaderGroupHandleSize;
        }

        return sShaderGroupHandleSize;
    }

    bool RtProgramVars::applyRtProgramVars(uint8_t* pRecord, const RtProgramVersion* pProgVersion, const RtStateObject* pRtso, ProgramVars* pVars, RtVarsContext* pContext)
    {
        const RtStateObject::ProgramList& programList = pRtso->getProgramList();

        // VKRayTODO: optimize this
        auto it = std::find_if(programList.begin(), programList.end(), [=](RtProgramVersion::SharedConstPtr prog)
        {
            return prog.get() == pProgVersion;
        });

        if (it == programList.end())
        {
            logError("Could not find program version in RtStateObject program list");
            return false;
        }

        uint32_t groupID = (uint32_t)std::distance(programList.begin(), it);

        vk_call(vkGetRayTracingShaderGroupHandlesNV(gpDevice->getApiHandle(), pRtso->getApiHandle(), groupID, 1, mProgramIdentifierSize, reinterpret_cast<void*>(pRecord)));
        pRecord += mProgramIdentifierSize;

        pContext->getRtVarsCmdList()->setRootParams(pProgVersion->getLocalRootSignature(), pRecord);
        return pVars->applyProgramVarsCommon<true>(pContext, true);
    }

    void RtVarsContext::apiInit()
    {
        mpList = RtVarsCmdList::create();

        // VKRayTODO: set proxy command list
        // mpLowLevelData->setCommandList(mpList.get());
    }
}
