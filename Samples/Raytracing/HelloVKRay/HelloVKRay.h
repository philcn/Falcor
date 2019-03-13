#pragma once
#include "Falcor.h"
#include "FalcorExperimental.h"

using namespace Falcor;

class HelloVKRay : public Renderer
{
public:
    void onLoad(SampleCallbacks* pSample, RenderContext* pRenderContext) override;
    void onFrameRender(SampleCallbacks* pSample, RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo) override;
    void onResizeSwapChain(SampleCallbacks* pSample, uint32_t width, uint32_t height) override;
    bool onKeyEvent(SampleCallbacks* pSample, const KeyboardEvent& keyEvent) override;
    bool onMouseEvent(SampleCallbacks* pSample, const MouseEvent& mouseEvent) override;
    void onGuiRender(SampleCallbacks* pSample, Gui* pGui) override;

private:
    RtScene::SharedPtr mRtScene;
    RtProgram::SharedPtr mRtProgram;
    RtState::SharedPtr mRtState;

    GraphicsVars::SharedPtr mpGlobalVars;

    Buffer::SharedPtr mShaderBindingTable;
    uint32_t mShaderRecordSize;

    Texture::SharedPtr mpRtOut;
};
