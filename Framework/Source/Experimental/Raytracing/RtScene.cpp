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
#include "RtScene.h"
#include "Graphics/Scene/SceneImporter.h"
#include "API/DescriptorSet.h"
#include "API/Device.h"

namespace Falcor
{
    RtScene::SharedPtr RtScene::loadFromFile(const std::string& filename, RtBuildFlags rtFlags, Model::LoadFlags modelLoadFlags, Scene::LoadFlags sceneLoadFlags)
    {
        RtScene::SharedPtr pRtScene = create(rtFlags);
        if (SceneImporter::loadScene(*pRtScene, filename, modelLoadFlags | Model::LoadFlags::BuffersAsShaderResource, sceneLoadFlags) == false)
        {
            pRtScene = nullptr;
        }

        for (auto& path : pRtScene->mpPaths)
        {
            for (uint32_t objIdx = 0u; objIdx < path->getAttachedObjectCount(); objIdx++)
            {
                const auto& it = pRtScene->mModelInstanceToRtModelInstance.find(path->getAttachedObject(objIdx).get());
                if (it != pRtScene->mModelInstanceToRtModelInstance.end())
                {
                    path->attachObject(it->second);
                }
            }
        }

        pRtScene->mModelInstanceToRtModelInstance.clear();
        return pRtScene;
    }

    RtScene::SharedPtr RtScene::create(RtBuildFlags rtFlags)
    {
        return SharedPtr(new RtScene(rtFlags));
    }

    RtScene::SharedPtr RtScene::createFromModel(RtModel::SharedPtr pModel)
    {
        SharedPtr pScene = RtScene::create(pModel->getBuildFlags());
        pScene->addModelInstance(pModel, "instance0");

        return pScene;
    }

    bool RtScene::update(double currentTime, CameraController* cameraController)
    {
        bool changed = Scene::update(currentTime, cameraController);
        mTlasHitProgCount = mExtentsDirty ? -1 : mTlasHitProgCount;

        if (mEnableRefit)
        {
            mRefit = true;
        }
        return changed;
    }

    void RtScene::addModelInstance(const ModelInstance::SharedPtr& pInstance)
    {
        RtModel::SharedPtr pRtModel = std::dynamic_pointer_cast<RtModel>(pInstance->getObject());
        if (pRtModel)
        {
            Scene::addModelInstance(pInstance);
        }
        else
        {
            // Check if we need to create a new model
            const auto& it = mModelToRtModel.find(pInstance->getObject().get());
            if (it == mModelToRtModel.end())
            {
                pRtModel = RtModel::createFromModel(*pInstance->getObject(), mRtFlags);
                mModelToRtModel[pInstance->getObject().get()] = pRtModel;
            }
            else
            {
                pRtModel = it->second;
            }
            ModelInstance::SharedPtr pRtInstance = ModelInstance::create(pRtModel, pInstance->getTranslation(), pInstance->getTarget(), pInstance->getUpVector(), pInstance->getScaling(), pInstance->getName());
            Scene::addModelInstance(pRtInstance);

            // any paths attached to this ModelInstance need to be updated
            auto pMovable = std::dynamic_pointer_cast<IMovableObject>(pInstance);
            auto pRtMovable = std::dynamic_pointer_cast<IMovableObject>(pRtInstance);

            mModelInstanceToRtModelInstance[pMovable.get()] = pRtMovable;
        }

#ifdef FALCOR_D3D12
        // If we have skinned models, attach a skinning cache and animate the scene once to trigger a VB update
        if (pRtModel->hasBones())
        {
            pRtModel->attachSkinningCache(mpSkinningCache);
            pRtModel->animate(0);
        }
#endif
    }
}