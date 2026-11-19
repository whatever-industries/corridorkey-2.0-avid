#pragma once

#include "avx2api.h"
#include "CACFUnknown.h"
#include "ACFPtr.h"

#include <array>

namespace corridor_key_mac {
enum class View : acfUInt32 { Composite = 0, Matte = 1, Original = 2 };
inline constexpr acfUInt32 kDefaultDespillAmount = 50;
inline constexpr acfUInt32 kColorCodeMax = 255;
inline constexpr std::array<acfUInt32, 3> kDefaultDespillColor = {0, kColorCodeMax, 0};
}

class CCorridorKey_Effect : public IAVXEffect, public CACFUnknown
{
protected:
    explicit CCorridorKey_Effect(IACFUnknown* controllingUnknown);
    ACFMETHODIMP InitializeInstance(IACFUnknown* host) override;
    ACFMETHODIMP InternalQueryInterface(const acfIID& iid, void** result) override;

public:
    ACFMETHODIMP GetEffectAttribute(
        const acfUID& id, IACFValue* value, IAVXEffectContext*, void*, void*,
        IAVXFrameContext*, void*) override;
    ACFMETHODIMP InitializeEffect(IAVXEffectContext*) override { return ACF_OK; }
    ACFMETHODIMP BeginEffect(IAVXEffectContext*, void**) override { return ACF_OK; }
    ACFMETHODIMP BeginFrameRange(IAVXEffectContext*, void*, void**) override { return ACF_OK; }
    ACFMETHODIMP BeginFrame(IAVXEffectContext*, void*, void*, IAVXFrameContext*, void**) override
    { return ACF_OK; }
    ACFMETHODIMP Render(
        IAVXEffectContext*, void*, void*, IAVXFrameContext*, void*,
        IAVXRenderContext*, IAVXOutput*) override;
    ACFMETHODIMP EndFrame(IAVXEffectContext*, void*, void*, IAVXFrameContext*, void*) override
    { return ACF_OK; }
    ACFMETHODIMP EndFrameRange(IAVXEffectContext*, void*, void*) override { return ACF_OK; }
    ACFMETHODIMP EndEffect(IAVXEffectContext*, void*) override { return ACF_OK; }

    ACF_DECLARE_CONCRETE();
};
