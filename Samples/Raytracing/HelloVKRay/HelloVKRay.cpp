#include "HelloVKRay.h"
#include "Framework.h"
#include "API/Vulkan/FalcorVK.h"
#include "Experimental/Raytracing/RtProgram/RtProgramVersion.h"
#include "Experimental/Raytracing/RtProgram/HitProgram.h"
#include "Experimental/Raytracing/RtProgram/SingleShaderProgram.h"
#include "Experimental/Raytracing/RtProgram/RtProgram.h"

struct Blob : ISlangBlob
{
    Blob(void* code, size_t size) : mCode(code), mSize(size) {}

    virtual void const* getBufferPointer() override { return mCode; }
    virtual size_t getBufferSize() override { return mSize; }

    virtual SlangResult queryInterface(SlangUUID const& uuid, void** outObject) override { return 0; }
    virtual uint32_t addRef() override { return 0; }
    virtual uint32_t release() override { return 0; }

    void* mCode;
    size_t mSize;
};

static RtShader::SharedPtr createShaderFromSPIRV(const std::string& filename, const std::string& entryPointName, ShaderType type)
{
    std::string fullpath;
    if (findFileInDataDirectories(filename, fullpath) == false)
    {
        msgBox("Error when loading shader. Can't find shader file " + filename);
        //could not find file
        return nullptr;
    }

    std::ifstream fileStream(fullpath, std::ios::binary | std::ios::in | std::ios::ate);
    assert(fileStream.is_open());

    const size_t shaderSize = fileStream.tellg();
    fileStream.seekg(0, std::ios::beg);
    std::vector<char> bytecode(shaderSize);
    fileStream.read(bytecode.data(), shaderSize);
    fileStream.close();

    Blob blob((void*)bytecode.data(), shaderSize);
    ComPtr<ISlangBlob> blob2(&blob);

    std::string log;
    auto shader = RtShader::create(blob2, entryPointName, type, Shader::CompilerFlags::None, log);
    return shader;
}

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
        gpDevice->flushAndSync();
    }

    DescriptorSet::Layout setLayout;
    setLayout.addRange(DescriptorSet::Type::AccelerationStructure, 0, 1);
    setLayout.addRange(DescriptorSet::Type::TextureUav, 1, 1);

    RootSignature::Desc desc;
    desc.addDescriptorSet(setLayout);
    mRootSignature = RootSignature::create(desc);

    auto swapchain = gpDevice->getSwapChainFbo();
    mRenderTarget = Texture::create2D(swapchain->getWidth(), swapchain->getHeight(), ResourceFormat::RGBA8Unorm, 1, 1, nullptr, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource);

    mDescriptorSet = DescriptorSet::create(gpDevice->getCpuDescriptorPool(), setLayout);
    mDescriptorSet->setAccelerationStructure(0, 0, mTlas);
    mDescriptorSet->setUav(1, 0, mRenderTarget->getUAV().get());

#if 1
    RtProgram::Desc programDesc;
    programDesc.addShaderLibrary("Data/raytrace.slang");
    programDesc.setRayGen("raygen");
    programDesc.addHitGroup(0, "closestHit", "anyHit");
    programDesc.addMiss(0, "miss");
    RtProgram::SharedPtr rtProgram = RtProgram::create(programDesc);
#endif

    CreatePipeline();
    CreateShaderBindingTable();
}

void HelloVKRay::CreatePipeline()
{
    RtShader::SharedPtr raygenShader = createShaderFromSPIRV("Data/rt_06_shaders.rgen.spv", "main", ShaderType::RayGeneration);
    RtShader::SharedPtr chitShader = createShaderFromSPIRV("Data/rt_06_shaders.rchit.spv", "main", ShaderType::ClosestHit);
    RtShader::SharedPtr missShader = createShaderFromSPIRV("Data/rt_06_shaders.rmiss.spv", "main", ShaderType::Miss);

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages(
        {
            raygenShader->getShaderStage(VK_SHADER_STAGE_RAYGEN_BIT_NV),
            chitShader->getShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV),
            missShader->getShaderStage(VK_SHADER_STAGE_MISS_BIT_NV),
        });

    std::vector<VkRayTracingShaderGroupCreateInfoNV> shaderGroups({
        // group0 = [ raygen ]
        { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 0, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV },
        // group1 = [ chit ]
        { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV, VK_SHADER_UNUSED_NV, 1, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV },
        // group2 = [ miss ]
        { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV, nullptr, VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 2, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV },
        });

    VkRayTracingPipelineCreateInfoNV rayPipelineInfo;
    rayPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
    rayPipelineInfo.pNext = nullptr;
    rayPipelineInfo.flags = 0;
    rayPipelineInfo.stageCount = (uint32_t)shaderStages.size();
    rayPipelineInfo.pStages = shaderStages.data();
    rayPipelineInfo.groupCount = (uint32_t)shaderGroups.size();
    rayPipelineInfo.pGroups = shaderGroups.data();
    rayPipelineInfo.maxRecursionDepth = 1;
    rayPipelineInfo.layout = mRootSignature->getApiHandle();
    rayPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    rayPipelineInfo.basePipelineIndex = 0;

    vk_call(Falcor::vkCreateRayTracingPipelinesNV(gpDevice->getApiHandle(), nullptr, 1, &rayPipelineInfo, nullptr, &_rtPipeline));
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
    vk_call(Falcor::vkGetRayTracingShaderGroupHandlesNV(gpDevice->getApiHandle(), _rtPipeline, 0, groupNum, shaderBindingTableSize, reinterpret_cast<void*>(shaderIdentifier.get())));

    mShaderBindingTable = Buffer::create(shaderBindingTableSize, Resource::BindFlags::ShaderResource, Buffer::CpuAccess::None /* can't be Write */, shaderIdentifier.get());
}

void HelloVKRay::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    const glm::vec4 clearColor(0.38f, 0.52f, 0.10f, 1);
    pRenderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

    pRenderContext->clearUAV(mRenderTarget->getUAV().get(), glm::vec4(0.0, 0.0, 0.0, 0.0));

    {
        VkCommandBuffer commandBuffer = pRenderContext->getLowLevelData()->getCommandList();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, _rtPipeline);

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
    VkDevice device = gpDevice->getApiHandle();

    if (_rtPipeline)
    {
        vkDestroyPipeline(device, _rtPipeline, nullptr);
    }
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
