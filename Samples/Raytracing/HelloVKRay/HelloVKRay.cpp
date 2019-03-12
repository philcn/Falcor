#include "HelloVKRay.h"
#include "Framework.h"
#include "API/Vulkan/FalcorVK.h"

void HelloVKRay::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
}

void HelloVKRay::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    initVKRtApi();

    {
        mRtModel = RtModel::createFromFile("teapot.obj", RtBuildFlags::None, Model::LoadFlags::None);
        mRtScene = RtScene::createFromModel(mRtModel);
        mTlas = mRtScene->getTlas(1);
    }

    {
        RtProgram::Desc programDesc;
        programDesc.addShaderLibrary("Data/raytrace.slang");
        programDesc.setRayGen("rayGen");
        programDesc.addHitGroup(0, "closestHit", "");
        programDesc.addMiss(0, "miss");
        mRtProgram = RtProgram::create(programDesc);

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

    {
        DescriptorSet::Layout setLayout;
        setLayout.addRange(DescriptorSet::Type::AccelerationStructure, 0, 1);
        setLayout.addRange(DescriptorSet::Type::TextureUav, 1, 1);

        RootSignature::Desc desc;
        desc.addDescriptorSet(setLayout);
        mRootSignature = RootSignature::create(desc);
        // mRootSignature = mRtProgram->getGlobalRootSignature();

        auto swapchain = gpDevice->getSwapChainFbo();
        mRenderTarget = Texture::create2D(swapchain->getWidth(), swapchain->getHeight(), ResourceFormat::RGBA8Unorm, 1, 1, nullptr, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource);

        mDescriptorSet = DescriptorSet::create(gpDevice->getCpuDescriptorPool(), setLayout);
        mDescriptorSet->setAccelerationStructure(0, 0, mTlas);
        mDescriptorSet->setUav(1, 0, mRenderTarget->getUAV().get());
    }

    {
        mRtState = RtState::create();
        mRtState->setMaxTraceRecursionDepth(10);
        mRtState->setProgram(mRtProgram);

        mCachedPipeline = mRtState->getRtso()->getApiHandle();
    }

    CreateShaderBindingTable();
}

void HelloVKRay::CreateShaderBindingTable()
{
    // Query values of shaderGroupHandleSize and maxRecursionDepth in current implementation 
    _rayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
    _rayTracingProperties.pNext = nullptr;
    _rayTracingProperties.maxRecursionDepth = 0;
    _rayTracingProperties.shaderGroupHandleSize = 0;
    VkPhysicalDeviceProperties2 props;
    props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props.pNext = &_rayTracingProperties;
    props.properties = { };
    vkGetPhysicalDeviceProperties2(gpDevice->getApiHandle(), &props);

    const uint32_t groupNum = 3; // 3 group is listed in pGroupNumbers in VkRayTracingPipelineCreateInfoNV
    const uint32_t shaderBindingTableSize = _rayTracingProperties.shaderGroupHandleSize * groupNum;

    std::unique_ptr<uint8_t[]> shaderIdentifier = std::make_unique<uint8_t[]>(shaderBindingTableSize);
    vk_call(Falcor::vkGetRayTracingShaderGroupHandlesNV(gpDevice->getApiHandle(), mCachedPipeline, 0, groupNum, shaderBindingTableSize, reinterpret_cast<void*>(shaderIdentifier.get())));

    mShaderBindingTable = Buffer::create(shaderBindingTableSize, Resource::BindFlags::ShaderResource, Buffer::CpuAccess::None /* can't be Write */, shaderIdentifier.get());
}

void HelloVKRay::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    const glm::vec4 clearColor(0.38f, 0.52f, 0.10f, 1);
    pRenderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

    pRenderContext->clearUAV(mRenderTarget->getUAV().get(), glm::vec4(0.0, 0.0, 0.0, 0.0));

    {
        VkCommandBuffer commandBuffer = pRenderContext->getLowLevelData()->getCommandList();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, mCachedPipeline);

        VkDescriptorSet set = mDescriptorSet->getApiHandle();
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, mRootSignature->getApiHandle(), 0, 1, &set, 0, 0);

        // Here's how the shader binding table looks like in this tutorial:
        // |[ raygen shader ]|[ hit shaders ]|[ miss shader ]|
        // |                 |               |               |
        // | 0               | 1             | 2             | 3

        Falcor::vkCmdTraceRaysNV(commandBuffer,
            mShaderBindingTable->getApiHandle(), 0,
            mShaderBindingTable->getApiHandle(), 2 * _rayTracingProperties.shaderGroupHandleSize, _rayTracingProperties.shaderGroupHandleSize,
            mShaderBindingTable->getApiHandle(), 1 * _rayTracingProperties.shaderGroupHandleSize, _rayTracingProperties.shaderGroupHandleSize,
            VK_NULL_HANDLE, 0, 0,
            mRenderTarget->getWidth(), mRenderTarget->getHeight(), 1);
    }

    pRenderContext->blit(mRenderTarget->getSRV(), pTargetFbo->getRenderTargetView(0));
}

void HelloVKRay::onShutdown(SampleCallbacks* pSample)
{
}

bool HelloVKRay::onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent)
{
    return false;
}

bool HelloVKRay::onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent)
{
    return false;
}

void HelloVKRay::onDataReload(SampleCallbacks* pSample)
{
}

void HelloVKRay::onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height)
{
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    HelloVKRay::UniquePtr pRenderer = std::make_unique<HelloVKRay>();
    SampleConfig config;
    config.deviceDesc.apiMajorVersion = 1;
    config.deviceDesc.apiMinorVersion = 1;
    config.deviceDesc.requiredExtensions.push_back("VK_NV_ray_tracing");
    config.windowDesc.title = "Hello VKRay";
    config.windowDesc.resizableWindow = true;
    Sample::run(config, pRenderer);

    return 0;
}
