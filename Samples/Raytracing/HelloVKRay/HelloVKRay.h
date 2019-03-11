#pragma once
#include "Falcor.h"
#include "Experimental/Raytracing/RtModel.h"
#include "Experimental/Raytracing/RtScene.h"
#include "Experimental/Raytracing/RtShader.h"

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
private:
    void CreatePipeline();
    void CreateShaderBindingTable();

    VkPipeline _rtPipeline = VK_NULL_HANDLE;
    VkPhysicalDeviceRayTracingPropertiesNV _rayTracingProperties = { };

    RtModel::SharedPtr mRtModel;
    RtScene::SharedPtr mRtScene;
    AccelerationStructureHandle mTlas;
    RootSignature::SharedPtr mRootSignature;
    DescriptorSet::Layout mSetLayout;
    DescriptorSet::SharedPtr mDescriptorSet;

    Buffer::SharedPtr mShaderBindingTable;
    Texture::SharedPtr mRenderTarget;
};
