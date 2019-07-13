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
#include "HelloVKRay.h"

static const glm::vec4 kClearColor(0.38f, 0.52f, 0.10f, 1);
static const char* kDefaultScene = "Arcade/Arcade.fscene";

static void reflectProgram(const RtProgram* program)
{
    auto globalReflector = program->getGlobalReflector();
    auto raygenReflector = program->getRayGenProgram()->getLocalReflector();
    auto hitReflector = program->getHitProgram(0)->getLocalReflector();

    auto listParameterBlocks = [](ProgramReflection* reflector)
    {
        for (int i = 0; i < reflector->getParameterBlockCount(); ++i)
        {
            auto pb = reflector->getParameterBlock(i);
            logWarning("Parameter block " + std::to_string(i) + ": " + pb->getName());
            for (int j = 0; j < pb->getResourceVec().size(); ++j)
            {
                auto res = pb->getResourceVec()[j];
                logWarning("Resource " + std::to_string(j) + ": " + res.name);
            }
        }
    };

    logWarning("Reflecting global: ");
    listParameterBlocks(globalReflector.get());

    logWarning("Reflecting raygen shader: ");
    listParameterBlocks(const_cast<ProgramReflection*>(raygenReflector.get()));

    logWarning("Reflecting hit shader: ");
    listParameterBlocks(const_cast<ProgramReflection*>(hitReflector.get()));
}

void HelloVKRay::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
    if (pGui->addButton("Load Scene"))
    {
        std::string filename;
        if (openFileDialog(Scene::kFileExtensionFilters, filename))
        {
            loadScene(filename, pSample->getCurrentFbo().get());
        }
    }
}

void HelloVKRay::loadScene(const std::string& filename, const Fbo* pTargetFbo)
{
    mpScene = RtScene::loadFromFile(filename, RtBuildFlags::None, Model::LoadFlags::None /* RemoveInstancing */);
    Model::SharedPtr pModel = mpScene->getModel(0);
    float radius = pModel->getRadius();

    mpCamera = mpScene->getActiveCamera();
    assert(mpCamera);

    mCamController.attachCamera(mpCamera);

    Sampler::Desc samplerDesc;
    samplerDesc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
    Sampler::SharedPtr pSampler = Sampler::create(samplerDesc);
    pModel->bindSamplerToMaterials(pSampler);

    // Update the controllers
    mCamController.setCameraSpeed(radius * 0.25f);
    float nearZ = std::max(0.1f, pModel->getRadius() / 750.0f);
    float farZ = radius * 10;
    mpCamera->setDepthRange(nearZ, farZ);
    mpCamera->setAspectRatio((float)pTargetFbo->getWidth() / (float)pTargetFbo->getHeight());

    mpRaytraceProgram->addDefine("RT_GEOMETRY_COUNT", std::to_string(mpScene->getGeometryCount(1)));
    mpRtVars = RtProgramVars::create(mpRaytraceProgram, mpScene);
}

void HelloVKRay::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    if (gpDevice->isFeatureSupported(Device::SupportedFeatures::Raytracing) == false)
    {
        logErrorAndExit("Device does not support raytracing!", true);
    }

    // VKRayTODO: move this somewhere else
    initVKRtApi();

    RtProgram::Desc rtProgDesc;
    rtProgDesc.addShaderLibrary("Data/HelloVKRay.slang").setRayGen("rayGen");
    rtProgDesc.addHitGroup(0, "closestHit", "");
    rtProgDesc.addMiss(0, "miss");

    mpRaytraceProgram = RtProgram::create(rtProgDesc);

    loadScene(kDefaultScene, pSample->getCurrentFbo().get());

    mpRtState = RtState::create();
    mpRtState->setProgram(mpRaytraceProgram);
    mpRtState->setMaxTraceRecursionDepth(3);

    mpRtRenderer = VKRtSceneRenderer::create(mpScene);
}

void HelloVKRay::setPerFrameVars(const Fbo* pTargetFbo)
{
    PROFILE("setPerFrameVars");
    GraphicsVars* pVars = mpRtVars->getGlobalVars().get();
    ConstantBuffer::SharedPtr pCB = pVars->getConstantBuffer("PerFrameCB");
    pCB["invView"] = glm::inverse(mpCamera->getViewMatrix());
    pCB["viewportDims"] = vec2(pTargetFbo->getWidth(), pTargetFbo->getHeight());
    float fovY = focalLengthToFovY(mpCamera->getFocalLength(), Camera::kDefaultFrameHeight);
    pCB["tanHalfFovY"] = tanf(fovY * 0.5f);
}

void HelloVKRay::renderRT(RenderContext* pContext, const Fbo* pTargetFbo)
{
    PROFILE("renderRT");
    setPerFrameVars(pTargetFbo);

    pContext->clearUAV(mpRtOut->getUAV().get(), kClearColor);
    mpRtVars->getGlobalVars()->setTexture("gOutput", mpRtOut);

    mpRtRenderer->renderScene(pContext, mpRtVars, mpRtState, uvec3(mpRtOut->getWidth(), mpRtOut->getHeight(), 1), mpCamera.get());

    pContext->blit(mpRtOut->getSRV(), pTargetFbo->getRenderTargetView(0));
}

void HelloVKRay::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    pRenderContext->clearFbo(pTargetFbo.get(), kClearColor, 1.0f, 0, FboAttachmentType::All);

    if(mpScene)
    {
        mCamController.update();

        renderRT(pRenderContext, pTargetFbo.get());
    }
}

bool HelloVKRay::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    if (mCamController.onKeyEvent(keyEvent))
    {
        return true;
    }
    return false;
}

bool HelloVKRay::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return mCamController.onMouseEvent(mouseEvent);
}

void HelloVKRay::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
    float h = (float)height;
    float w = (float)width;

    mpCamera->setFocalLength(18);
    float aspectRatio = (w / h);
    mpCamera->setAspectRatio(aspectRatio);

    mpRtOut = Texture::create2D(width, height, ResourceFormat::RGBA16Float, 1, 1, nullptr, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource);
}

#ifdef _WIN32
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int main(int argc, char** argv)
#endif
{
    HelloVKRay::UniquePtr pRenderer = std::make_unique<HelloVKRay>();
    SampleConfig config;
    // For vkGetPhysicalDeviceProperties2()
    config.deviceDesc.apiMajorVersion = 1;
    config.deviceDesc.apiMinorVersion = 1;
    config.deviceDesc.requiredExtensions.push_back("VK_NV_ray_tracing");
    config.windowDesc.title = "Hello VKRay";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);

    return 0;
}
