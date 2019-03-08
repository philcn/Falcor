#include "HelloVKRay.h"
#include "Framework.h"
#include "API/Vulkan/FalcorVK.h"

#define NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, funcName) \
    { \
        funcName = reinterpret_cast<PFN_##funcName>(vkGetDeviceProcAddr(device, ""#funcName)); \
        if (funcName == nullptr) \
        { \
            const std::string name = #funcName; \
            logErrorAndExit(std::string("Can't get device function address: ") + name); \
        } \
    }

#define NVVK_CHECK_ERROR(code, message) { if (code != VK_SUCCESS) { logErrorAndExit(message + std::string(" ErrorCode: ") + std::to_string(code)); } }

class RtShader : public Shader
{
public:
    using SharedPtr = std::shared_ptr<RtShader>;

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

    ~RtShader() {}

    const std::string& getEntryPoint() const { return mEntryPoint; }
    VkPipelineShaderStageCreateInfo getShaderStage(VkShaderStageFlagBits stage);

    static SharedPtr create(const std::string& filename, const std::string& entryPointName, ShaderType type);

private:
    RtShader(ShaderType type, const std::string& entryPointName) : Shader(type), mEntryPoint(entryPointName) {}
    std::string mEntryPoint;
};

RtShader::SharedPtr RtShader::create(const std::string& filename, const std::string& entryPointName, ShaderType type)
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
    SharedPtr pShader = SharedPtr(new RtShader(type, entryPointName));
    return pShader->init(blob2, entryPointName, Shader::CompilerFlags::None, log) ? pShader : nullptr;
}

VkPipelineShaderStageCreateInfo RtShader::getShaderStage(VkShaderStageFlagBits stage)
{
    VkPipelineShaderStageCreateInfo result;
    result.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    result.pNext = nullptr;
    result.stage = stage;
    result.module = mApiHandle;
    result.pName = mEntryPoint.c_str();
    result.flags = 0;
    result.pSpecializationInfo = nullptr;
    return result;
}


void HelloVKRay::onGuiRender(SampleCallbacks* pSample, Gui* pGui)
{
}

void HelloVKRay::onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext)
{
    InitRayTracing();
    CreateAccelerationStructures();
    CreatePipeline();
    CreateDescriptorSet();
    CreateShaderBindingTable();
}

void HelloVKRay::InitRayTracing()
{
    // Get ray_tracing function pointers
    VkDevice device = gpDevice->getApiHandle();
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkCreateAccelerationStructureNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkDestroyAccelerationStructureNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkGetAccelerationStructureMemoryRequirementsNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkCmdCopyAccelerationStructureNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkBindAccelerationStructureMemoryNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkCmdBuildAccelerationStructureNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkCmdTraceRaysNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkGetRayTracingShaderGroupHandlesNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkCreateRayTracingPipelinesNV);
    NVVK_RESOLVE_DEVICE_FUNCTION_ADDRESS(device, vkGetAccelerationStructureHandleNV);

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
}

void HelloVKRay::CreateAccelerationStructures()
{
    // CREATE GEOMETRY
    // Notice that vertex/index buffers have to be alive while
    // geometry is used because it references them
    Buffer::SharedPtr vertexBuffer;
    Buffer::SharedPtr indexBuffer;
    std::vector<VkGeometryNV> geometries;

    {
        struct Vertex
        {
            float X, Y, Z;
        };

        std::vector<Vertex> vertices
        {
            { -0.5f, -0.5f, 0.0f },
            { +0.0f, +0.5f, 0.0f },
            { +0.5f, -0.5f, 0.0f }
        };
        const uint32_t vertexCount = (uint32_t)vertices.size();
        const VkDeviceSize vertexSize = sizeof(Vertex);
        const VkDeviceSize vertexBufferSize = vertexCount * vertexSize;

        std::vector<uint16_t> indices
        {
            { 0, 1, 2 }
        };
        const uint32_t indexCount = (uint32_t)indices.size();
        const VkDeviceSize indexSize = sizeof(uint16_t);
        const VkDeviceSize indexBufferSize = indexCount * indexSize;

        vertexBuffer = Buffer::create(vertexBufferSize, Resource::BindFlags::Vertex, Buffer::CpuAccess::None, vertices.data());
        indexBuffer = Buffer::create(indexBufferSize, Resource::BindFlags::Index, Buffer::CpuAccess::None, indices.data());

        VkGeometryNV geometry;
        geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
        geometry.pNext = nullptr;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
        geometry.geometry.triangles.pNext = nullptr;
        geometry.geometry.triangles.vertexData = vertexBuffer->getApiHandle();
        geometry.geometry.triangles.vertexOffset = 0;
        geometry.geometry.triangles.vertexCount = vertexCount;
        geometry.geometry.triangles.vertexStride = vertexSize;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.indexData = indexBuffer->getApiHandle();
        geometry.geometry.triangles.indexOffset = 0;
        geometry.geometry.triangles.indexCount = indexCount;
        geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
        geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
        geometry.geometry.triangles.transformOffset = 0;
        geometry.geometry.aabbs = { };
        geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
        geometry.flags = 0;

        geometries.emplace_back(geometry);
    }

    // CREATE BOTTOM LEVEL ACCELERATION STRUCTURES
    auto CreateAccelerationStructure = [&](VkAccelerationStructureTypeNV type, uint32_t geometryCount,
        VkGeometryNV* geometries, uint32_t instanceCount, VkAccelerationStructureNV& AS, VkDeviceMemory& memory)
    {
        VkAccelerationStructureCreateInfoNV accelerationStructureInfo;
        accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
        accelerationStructureInfo.pNext = nullptr;
        accelerationStructureInfo.compactedSize = 0;
        accelerationStructureInfo.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
        accelerationStructureInfo.info.pNext = NULL;
        accelerationStructureInfo.info.type = type;
        accelerationStructureInfo.info.flags = 0;
        accelerationStructureInfo.info.instanceCount = instanceCount;
        accelerationStructureInfo.info.geometryCount = geometryCount;
        accelerationStructureInfo.info.pGeometries = geometries;

        VkResult code = vkCreateAccelerationStructureNV(gpDevice->getApiHandle(), &accelerationStructureInfo, nullptr, &AS);
        NVVK_CHECK_ERROR(code, "vkCreateAccelerationStructureNV");

        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo;
        memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.pNext = nullptr;
        memoryRequirementsInfo.accelerationStructure = AS;
        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;

        VkMemoryRequirements2 memoryRequirements;
        vkGetAccelerationStructureMemoryRequirementsNV(gpDevice->getApiHandle(), &memoryRequirementsInfo, &memoryRequirements);

        VkMemoryAllocateInfo memoryAllocateInfo;
        memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memoryAllocateInfo.pNext = nullptr;
        memoryAllocateInfo.allocationSize = memoryRequirements.memoryRequirements.size;
        memoryAllocateInfo.memoryTypeIndex = gpDevice->getVkMemoryType(Device::MemoryType::Default, memoryRequirements.memoryRequirements.memoryTypeBits);

        code = vkAllocateMemory(gpDevice->getApiHandle(), &memoryAllocateInfo, nullptr, &memory);
        NVVK_CHECK_ERROR(code, "rt AS vkAllocateMemory");

        VkBindAccelerationStructureMemoryInfoNV bindInfo;
        bindInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
        bindInfo.pNext = nullptr;
        bindInfo.accelerationStructure = AS;
        bindInfo.memory = memory;
        bindInfo.memoryOffset = 0;
        bindInfo.deviceIndexCount = 0;
        bindInfo.pDeviceIndices = nullptr;

        code = vkBindAccelerationStructureMemoryNV(gpDevice->getApiHandle(), 1, &bindInfo);
        NVVK_CHECK_ERROR(code, "vkBindAccelerationStructureMemoryNV");
    };

    CreateAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
        (uint32_t)geometries.size(), geometries.data(), 0,
        _bottomAS, _bottomASMemory);

    // CREATE INSTANCE BUFFER
    Buffer::SharedPtr instanceBuffer;

    {
        uint64_t accelerationStructureHandle;
        VkResult code = vkGetAccelerationStructureHandleNV(gpDevice->getApiHandle(), _bottomAS, sizeof(uint64_t), &accelerationStructureHandle);
        NVVK_CHECK_ERROR(code, "vkGetAccelerationStructureHandleNV");

        float transform[12] =
        {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
        };

        VkGeometryInstance instance;
        memcpy(instance.transform, transform, sizeof(instance.transform));
        instance.instanceId = 0;
        instance.mask = 0xff;
        instance.instanceOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
        instance.accelerationStructureHandle = accelerationStructureHandle;

        uint32_t instanceBufferSize = (uint32_t)sizeof(VkGeometryInstance);
        instanceBuffer = Buffer::create(instanceBufferSize, Resource::BindFlags::RayTracing, Buffer::CpuAccess::None, &instance);
    }

    // CREATE TOP LEVEL ACCELERATION STRUCTURES
    CreateAccelerationStructure(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
        0, nullptr, 1,
        _topAS, _topASMemory);

    // BUILD ACCELERATION STRUCTURES
    auto GetScratchBufferSize = [&](VkAccelerationStructureNV handle)
    {
        VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo;
        memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
        memoryRequirementsInfo.pNext = nullptr;
        memoryRequirementsInfo.accelerationStructure = handle;
        memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

        VkMemoryRequirements2 memoryRequirements;
        vkGetAccelerationStructureMemoryRequirementsNV(gpDevice->getApiHandle(), &memoryRequirementsInfo, &memoryRequirements);

        VkDeviceSize result = memoryRequirements.memoryRequirements.size;
        return result;
    };

    {
        VkCommandBuffer commandBuffer = gpDevice->getRenderContext()->getLowLevelData()->getCommandList();

        VkDeviceSize bottomAccelerationStructureBufferSize = GetScratchBufferSize(_bottomAS);
        VkDeviceSize topAccelerationStructureBufferSize = GetScratchBufferSize(_topAS);
        VkDeviceSize scratchBufferSize = std::max(bottomAccelerationStructureBufferSize, topAccelerationStructureBufferSize);

        Buffer::SharedPtr scratchBuffer = Buffer::create(scratchBufferSize, Resource::BindFlags::RayTracing, Buffer::CpuAccess::None);

        VkMemoryBarrier memoryBarrier;
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.pNext = nullptr;
        memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
        memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

        {
            VkAccelerationStructureInfoNV asInfo;
            asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
            asInfo.pNext = NULL;
            asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
            asInfo.flags = 0;
            asInfo.instanceCount = 0;
            asInfo.geometryCount = (uint32_t)geometries.size();
            asInfo.pGeometries = &geometries[0];
            vkCmdBuildAccelerationStructureNV(commandBuffer, &asInfo, VK_NULL_HANDLE, 0, VK_FALSE, _bottomAS, VK_NULL_HANDLE, scratchBuffer->getApiHandle(), 0);
        }

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

        {
            VkAccelerationStructureInfoNV asInfo;
            asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
            asInfo.pNext = NULL;
            asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
            asInfo.flags = 0;
            asInfo.instanceCount = 1;
            asInfo.geometryCount = 0;
            asInfo.pGeometries = nullptr;
            vkCmdBuildAccelerationStructureNV(commandBuffer, &asInfo, instanceBuffer->getApiHandle(), 0, VK_FALSE, _topAS, VK_NULL_HANDLE, scratchBuffer->getApiHandle(), 0);
        }

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

        gpDevice->flushAndSync();
    }
}

void HelloVKRay::CreatePipeline()
{
    // CREATE DESCRIPTOR SET LAYOUT
    {
        VkDescriptorSetLayoutBinding accelerationStructureLayoutBinding;
        accelerationStructureLayoutBinding.binding = 0;
        accelerationStructureLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
        accelerationStructureLayoutBinding.descriptorCount = 1;
        accelerationStructureLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
        accelerationStructureLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding outputImageLayoutBinding;
        outputImageLayoutBinding.binding = 1;
        outputImageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        outputImageLayoutBinding.descriptorCount = 1;
        outputImageLayoutBinding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_NV;
        outputImageLayoutBinding.pImmutableSamplers = nullptr;

        std::vector<VkDescriptorSetLayoutBinding> bindings({ accelerationStructureLayoutBinding, outputImageLayoutBinding });

        VkDescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = nullptr;
        layoutInfo.flags = 0;
        layoutInfo.bindingCount = (uint32_t)(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VkResult code = vkCreateDescriptorSetLayout(gpDevice->getApiHandle(), &layoutInfo, nullptr, &_rtDescriptorSetLayout);
        NVVK_CHECK_ERROR(code, "rt vkCreateDescriptorSetLayout");
    }

    // CREATE PIPELINE
    {
        RtShader::SharedPtr raygenShader = RtShader::create("Data/rt_06_shaders.rgen.spv", "main", ShaderType::RayGeneration);
        RtShader::SharedPtr chitShader = RtShader::create("Data/rt_06_shaders.rchit.spv", "main", ShaderType::ClosestHit);
        RtShader::SharedPtr missShader = RtShader::create("Data/rt_06_shaders.rmiss.spv", "main", ShaderType::Miss);

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages(
            {
                raygenShader->getShaderStage(VK_SHADER_STAGE_RAYGEN_BIT_NV),
                chitShader->getShaderStage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV),
                missShader->getShaderStage(VK_SHADER_STAGE_MISS_BIT_NV),
            });

        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.pNext = nullptr;
        pipelineLayoutCreateInfo.flags = 0;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = &_rtDescriptorSetLayout;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

        VkResult code = vkCreatePipelineLayout(gpDevice->getApiHandle(), &pipelineLayoutCreateInfo, nullptr, &_rtPipelineLayout);
        NVVK_CHECK_ERROR(code, "rt vkCreatePipelineLayout");

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
        rayPipelineInfo.layout = _rtPipelineLayout;
        rayPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        rayPipelineInfo.basePipelineIndex = 0;

        code = vkCreateRayTracingPipelinesNV(gpDevice->getApiHandle(), nullptr, 1, &rayPipelineInfo, nullptr, &_rtPipeline);
        NVVK_CHECK_ERROR(code, "vkCreateRayTracingPipelinesNV");
    }
}

void HelloVKRay::CreateShaderBindingTable()
{
    const uint32_t groupNum = 3; // 3 group is listed in pGroupNumbers in VkRayTracingPipelineCreateInfoNV
    const uint32_t shaderBindingTableSize = _rayTracingProperties.shaderGroupHandleSize * groupNum;

    std::unique_ptr<uint8_t[]> shaderIdentifier = std::make_unique<uint8_t[]>(shaderBindingTableSize);
    auto code = vkGetRayTracingShaderGroupHandlesNV(gpDevice->getApiHandle(), _rtPipeline, 0, groupNum, shaderBindingTableSize, reinterpret_cast<void*>(shaderIdentifier.get()));
    NVVK_CHECK_ERROR(code, "vkGetRayTracingShaderHandleNV");

    _shaderBindingTable = Buffer::create(shaderBindingTableSize, Resource::BindFlags::ShaderResource, Buffer::CpuAccess::None /* can't be Write */, shaderIdentifier.get());
}

void HelloVKRay::CreateDescriptorSet()
{
    std::vector<VkDescriptorPoolSize> poolSizes
    ({
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1 }
    });

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo;
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.pNext = nullptr;
    descriptorPoolCreateInfo.flags = 0;
    descriptorPoolCreateInfo.maxSets = 1;
    descriptorPoolCreateInfo.poolSizeCount = (uint32_t)poolSizes.size();
    descriptorPoolCreateInfo.pPoolSizes = poolSizes.data();

    VkResult code = vkCreateDescriptorPool(gpDevice->getApiHandle(), &descriptorPoolCreateInfo, nullptr, &_rtDescriptorPool);
    NVVK_CHECK_ERROR(code, "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo;
    descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocateInfo.pNext = nullptr;
    descriptorSetAllocateInfo.descriptorPool = _rtDescriptorPool;
    descriptorSetAllocateInfo.descriptorSetCount = 1;
    descriptorSetAllocateInfo.pSetLayouts = &_rtDescriptorSetLayout;

    code = vkAllocateDescriptorSets(gpDevice->getApiHandle(), &descriptorSetAllocateInfo, &_rtDescriptorSet);
    NVVK_CHECK_ERROR(code, "vkAllocateDescriptorSets");

    VkWriteDescriptorSetAccelerationStructureNV descriptorAccelerationStructureInfo;
    descriptorAccelerationStructureInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
    descriptorAccelerationStructureInfo.pNext = nullptr;
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures = &_topAS;

    VkWriteDescriptorSet accelerationStructureWrite;
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo; // Notice that pNext is assigned here!
    accelerationStructureWrite.dstSet = _rtDescriptorSet;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.dstArrayElement = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
    accelerationStructureWrite.pImageInfo = nullptr;
    accelerationStructureWrite.pBufferInfo = nullptr;
    accelerationStructureWrite.pTexelBufferView = nullptr;

    auto swapchain = gpDevice->getSwapChainFbo();
    _renderTarget = Texture::create2D(swapchain->getWidth(), swapchain->getHeight(), ResourceFormat::RGBA8Unorm, 1, 1, nullptr, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::ShaderResource);
    VkImageView imageView = _renderTarget->getUAV()->getApiHandle();

    VkDescriptorImageInfo descriptorOutputImageInfo;
    descriptorOutputImageInfo.sampler = nullptr;
    descriptorOutputImageInfo.imageView = imageView;
    descriptorOutputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet outputImageWrite;
    outputImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    outputImageWrite.pNext = nullptr;
    outputImageWrite.dstSet = _rtDescriptorSet;
    outputImageWrite.dstBinding = 1;
    outputImageWrite.dstArrayElement = 0;
    outputImageWrite.descriptorCount = 1;
    outputImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputImageWrite.pImageInfo = &descriptorOutputImageInfo;
    outputImageWrite.pBufferInfo = nullptr;
    outputImageWrite.pTexelBufferView = nullptr;

    std::vector<VkWriteDescriptorSet> descriptorWrites({ accelerationStructureWrite, outputImageWrite });
    vkUpdateDescriptorSets(gpDevice->getApiHandle(), (uint32_t)descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void HelloVKRay::onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
{
    const glm::vec4 clearColor(0.38f, 0.52f, 0.10f, 1);
    pRenderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

    pRenderContext->clearUAV(_renderTarget->getUAV().get(), glm::vec4(0.0, 0.0, 0.0, 0.0));

    {
        VkCommandBuffer commandBuffer = pRenderContext->getLowLevelData()->getCommandList();

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, _rtPipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, _rtPipelineLayout, 0, 1, &_rtDescriptorSet, 0, 0);

        // Here's how the shader binding table looks like in this tutorial:
        // |[ raygen shader ]|[ hit shaders ]|[ miss shader ]|
        // |                 |               |               |
        // | 0               | 1             | 2             | 3

        vkCmdTraceRaysNV(commandBuffer,
            _shaderBindingTable->getApiHandle(), 0,
            _shaderBindingTable->getApiHandle(), 2 * _rayTracingProperties.shaderGroupHandleSize, _rayTracingProperties.shaderGroupHandleSize,
            _shaderBindingTable->getApiHandle(), 1 * _rayTracingProperties.shaderGroupHandleSize, _rayTracingProperties.shaderGroupHandleSize,
            VK_NULL_HANDLE, 0, 0,
            _renderTarget->getWidth(), _renderTarget->getHeight(), 1);
    }

    pRenderContext->blit(_renderTarget->getSRV(), pTargetFbo->getRenderTargetView(0));
}

void HelloVKRay::onShutdown(SampleCallbacks* pSample)
{
    VkDevice device = gpDevice->getApiHandle();

    if (_topAS)
    {
        vkDestroyAccelerationStructureNV(device, _topAS, nullptr);
    }
    if (_topASMemory)
    {
        vkFreeMemory(device, _topASMemory, nullptr);
    }
    if (_bottomAS)
    {
        vkDestroyAccelerationStructureNV(device, _bottomAS, nullptr);
    }
    if (_bottomASMemory)
    {
        vkFreeMemory(device, _bottomASMemory, nullptr);
    }
    if (_rtDescriptorPool)
    {
        vkDestroyDescriptorPool(device, _rtDescriptorPool, nullptr);
    }
    if (_rtPipeline)
    {
        vkDestroyPipeline(device, _rtPipeline, nullptr);
    }
    if (_rtPipelineLayout)
    {
        vkDestroyPipelineLayout(device, _rtPipelineLayout, nullptr);
    }
    if (_rtDescriptorSetLayout)
    {
        vkDestroyDescriptorSetLayout(device, _rtDescriptorSetLayout, nullptr);
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
