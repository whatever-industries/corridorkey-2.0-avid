#pragma once

#include "avx2api.h"
#include "CACFUnknown.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct PreviewProbeInput {
    std::string description;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool visible = false;
};

void ShowPreviewProbe(acfUInt32 frame, const std::array<PreviewProbeInput, 2>& inputs, bool distinct);

class CorridorKeyPreviewProbe : public IAVXModalUI, public CACFUnknown {
public:
    ACFMETHODIMP DoDialog(IAVXEffectContext*, IAVXFrameContext*, IAVXRenderContext*) override;
    ACF_DECLARE_CONCRETE();

protected:
    explicit CorridorKeyPreviewProbe(IACFUnknown* controllingUnknown) : CACFUnknown(controllingUnknown) {}
    ACFMETHODIMP InitializeInstance(IACFUnknown* host) override { return host ? ACF_OK : ACF_E_POINTER; }
    ACFMETHODIMP InternalQueryInterface(const acfIID& iid, void** result) override;
};
