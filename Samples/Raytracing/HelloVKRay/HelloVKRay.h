#pragma once
#include "Falcor.h"

using namespace Falcor;

class HelloVKRay : public Renderer
{
public:
    void onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext) override;
    void onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onShutdown(SampleCallbacks* pSample) override;
    void onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent) override;
    void onDataReload(SampleCallbacks* pSample) override;
    void onGuiRender(SampleCallbacks* pSample, Gui* pGui) override;

protected:
    PFN_vkCreateAccelerationStructureNV vkCreateAccelerationStructureNV = VK_NULL_HANDLE;
    PFN_vkDestroyAccelerationStructureNV vkDestroyAccelerationStructureNV = VK_NULL_HANDLE;
    PFN_vkGetAccelerationStructureMemoryRequirementsNV vkGetAccelerationStructureMemoryRequirementsNV = VK_NULL_HANDLE;
    PFN_vkCmdCopyAccelerationStructureNV vkCmdCopyAccelerationStructureNV = VK_NULL_HANDLE;
    PFN_vkBindAccelerationStructureMemoryNV vkBindAccelerationStructureMemoryNV = VK_NULL_HANDLE;
    PFN_vkCmdBuildAccelerationStructureNV vkCmdBuildAccelerationStructureNV = VK_NULL_HANDLE;
    PFN_vkCmdTraceRaysNV vkCmdTraceRaysNV = VK_NULL_HANDLE;
    PFN_vkGetRayTracingShaderGroupHandlesNV vkGetRayTracingShaderGroupHandlesNV = VK_NULL_HANDLE;
    PFN_vkCreateRayTracingPipelinesNV vkCreateRayTracingPipelinesNV = VK_NULL_HANDLE;
    PFN_vkGetAccelerationStructureHandleNV vkGetAccelerationStructureHandleNV = VK_NULL_HANDLE;

    VkPhysicalDeviceRayTracingPropertiesNV _rayTracingProperties = { };

    void InitRayTracing();
    void CreateAccelerationStructures();
    void CreatePipeline();
    void CreateDescriptorSet();
    void CreateShaderBindingTable();

private:
    VkDeviceMemory _topASMemory = VK_NULL_HANDLE;
    VkAccelerationStructureNV _topAS = VK_NULL_HANDLE;
    VkDeviceMemory _bottomASMemory = VK_NULL_HANDLE;
    VkAccelerationStructureNV _bottomAS = VK_NULL_HANDLE;

    VkDescriptorSetLayout _rtDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout _rtPipelineLayout = VK_NULL_HANDLE;
    VkPipeline _rtPipeline = VK_NULL_HANDLE;

    Buffer::SharedPtr _shaderBindingTable;

    VkDescriptorPool _rtDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _rtDescriptorSet = VK_NULL_HANDLE;

    Texture::SharedPtr _renderTarget;
};

struct VkGeometryInstance
{
    float transform[12];
    uint32_t instanceId : 24;
    uint32_t mask : 8;
    uint32_t instanceOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureHandle;
};
