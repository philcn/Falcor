#include "HelloVKRay.h"

static const glm::vec4 kClearColor(0.38f, 0.52f, 0.10f, 1);
static const char* kDefaultScene = "teapot.obj";

void HelloVKRay::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
}

void HelloVKRay::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    if (gpDevice->isFeatureSupported(Device::SupportedFeatures::Raytracing) == false)
    {
        logErrorAndExit("Device does not support raytracing!", true);
    }

    // VKRayTODO: move this somewhere else
    initVKRtApi();

    {
        RtModel::SharedPtr model = RtModel::createFromFile(kDefaultScene, RtBuildFlags::None, Model::LoadFlags::None);
        mRtScene = RtScene::createFromModel(model);

        RtProgram::Desc programDesc;
        programDesc.addShaderLibrary("Data/HelloVKRay.slang");
        programDesc.setRayGen("rayGen");
        programDesc.addHitGroup(0, "closestHit", "");
        programDesc.addMiss(0, "miss");
        mRtProgram = RtProgram::create(programDesc);

        mRtState = RtState::create();
        mRtState->setMaxTraceRecursionDepth(3);
        mRtState->setProgram(mRtProgram);

        mRtVars = RtProgramVars::create(mRtProgram, mRtScene);

        ParameterBlockReflection::BindLocation loc = mRtVars->getGlobalVars()->getReflection()->getDefaultParameterBlock()->getResourceBinding("gRtScene");
        if (loc.setIndex != ProgramReflection::kInvalidLocation)
        {
            mRtVars->getGlobalVars()->getDefaultBlock()->setAccelerationStructure(loc, 0, mRtScene->getTlas(mRtProgram->getHitProgramCount()));
        }
    }

    #if 0
    {
        auto globalReflector = mRtProgram->getGlobalReflector();
        auto raygenReflector = mRtProgram->getRayGenProgram()->getLocalReflector();
        auto hitReflector = mRtProgram->getHitProgram(0)->getLocalReflector();
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
    #endif
}

void HelloVKRay::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    pRenderContext->clearFbo(pTargetFbo.get(), kClearColor, 1.0f, 0, FboAttachmentType::All);
    pRenderContext->clearUAV(mpRtOut->getUAV().get(), kClearColor);

    mRtVars->apply(pRenderContext, mRtState->getRtso().get());

    // VKRayTODO: need to support bind root sets for ray tracing before we can remove this
    VkDescriptorSet set = mRtVars->getGlobalVars()->getDefaultBlock()->getRootSets().front().pSet->getApiHandle();
    vkCmdBindDescriptorSets(pRenderContext->getLowLevelData()->getCommandList(), VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, mRtProgram->getGlobalRootSignature()->getApiHandle(), 0, 1, &set, 0, 0);

    pRenderContext->raytrace(mRtVars, mRtState, mpRtOut->getWidth(), mpRtOut->getHeight(), 1);
    pRenderContext->blit(mpRtOut->getSRV(), pTargetFbo->getRenderTargetView(0));
}

bool HelloVKRay::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    return false;
}

bool HelloVKRay::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return false;
}

void HelloVKRay::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
    mpRtOut = Texture::create2D(width, height, ResourceFormat::RGBA16Float, 1, 1, nullptr, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource);
    mRtVars->getGlobalVars()->setTexture("gOutput", mpRtOut);
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
