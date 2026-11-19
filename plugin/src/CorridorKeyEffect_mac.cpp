#include "CorridorKeyEffect_mac.h"
#include "acfextras.h"
#include "acfvalue.h"
#ifdef CK_PREVIEW_PROBE
#include "PreviewProbe_mac.h"
#include <os/log.h>
#include <pthread.h>
#endif
#ifndef CK_SYNTHETIC_MATTE
#include "CorridorKeyUIDs.h"
#include "MacFrameWorker.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <string_view>
#include <CoreFoundation/CFUserNotification.h>
#include <os/log.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

ACF_DEFINE_FACTORY(CCorridorKey_Effect)

namespace {

#ifdef CK_EXPLICIT_COMPOSITE
// Media Composer's Key category supplies the lower track first.
constexpr acfUInt32 kBackgroundInput = 0;
constexpr acfUInt32 kForegroundInput = 1;
#else
constexpr acfUInt32 kForegroundInput = 0;
#endif

struct RgbLayout {
    acfUInt32 stride;
    std::array<acfSInt32, 3> rgb;
    acfSInt32 alpha;
    bool fixedPoint;
    float black;
    float colorSpan;
};

constexpr float kFixedPointOne = 16384.0f;

bool ReadLayout(IAVXImage* image, RgbLayout& layout)
{
    ACFPtr<IAVXImageFormat> format;
    if (ACFFAILED(image->GetFormat(&format)) || !format)
        return false;
    const auto type = format->GetComponentType();
    const bool fixedPoint = type == kAVXComponentType_SHORT_2_14;
    if (!fixedPoint && type != kAVXComponentType_UCHAR)
        return false;
    // AVX 2.14 RGB is normalized to 0–1, including when the source media
    // uses video levels. Only the 8-bit buffer uses encoded black/white.
    const bool videoRange = !fixedPoint &&
        format->GetBlackWhiteLevel() == kAVXBlackWhiteLevel_VideoRange;
    layout = {format->GetPixelSize(0),
              {format->GetChannel(kAVXChannelType_Red),
               format->GetChannel(kAVXChannelType_Green),
               format->GetChannel(kAVXChannelType_Blue)},
              format->GetChannel(kAVXChannelType_Alpha),
              fixedPoint,
              videoRange ? 16.0f : 0.0f,
              fixedPoint ? kFixedPointOne : (videoRange ? 219.0f : 255.0f)};
    const auto componentSize = fixedPoint ? sizeof(avx2Pixel2_14) : sizeof(avx2Pixel8);
    const auto componentCount = layout.stride / componentSize;
    for (auto channel : layout.rgb) {
        if (channel < 0 || static_cast<acfUInt32>(channel) >= componentCount)
            return false;
    }
    return layout.alpha < 0 || static_cast<acfUInt32>(layout.alpha) < componentCount;
}

ACFRESULT GetInputImage(IAVXEffectContext* context, acfUInt32 index, acfUInt32 frame,
                        avx2Field field, avx2FieldMode mode, ACFPtr<IAVXImage>& image)
{
    ACFPtr<IAVXImageInput> input;
    auto result = context->GetInput(index, AVXInputTypeID_Image, IID_PPV_ARG(IAVXImageInput, &input));
    if (ACFFAILED(result) || !input)
        return ACFFAILED(result) ? result : ACF_E_DATANOTAVAILABLE;
    return input->GetImage(frame, field, mode, &image);
}

float ReadColor(const avx2Pixel8* pixel, acfSInt32 channel, const RgbLayout& layout)
{
    if (layout.fixedPoint) {
        avx2Pixel2_14 sample;
        std::memcpy(&sample, pixel + channel * sizeof(sample), sizeof(sample));
        return sample / kFixedPointOne;
    }
    return (pixel[channel] - layout.black) / layout.colorSpan;
}

#ifdef CK_PREVIEW_PROBE
PreviewProbeInput SampleInput(IAVXImage* image, const char* source)
{
    RgbLayout layout{};
    const auto width = image->GetWidth();
    const auto height = image->GetHeight();
    if (!width || !height || !ReadLayout(image, layout))
        return {"Unsupported or empty RGB frame"};

    const auto scale = std::min({1.0, 288.0 / width, 168.0 / height});
    PreviewProbeInput result;
    result.width = std::max(1, static_cast<int>(scale * width));
    result.height = std::max(1, static_cast<int>(scale * height));
    result.rgba.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    auto* target = result.rgba.data();
    std::uint8_t brightest = 0;
    for (int y = 0; y < result.height; ++y) {
        const auto* line = image->GetLinePtr(y * height / result.height, 0);
        if (!line)
            return {"Frame has no readable pixels"};
        for (int x = 0; x < result.width; ++x) {
            const auto* pixel = line + (x * width / result.width) * layout.stride;
            for (auto channel : layout.rgb) {
                const auto value = std::clamp(ReadColor(pixel, channel, layout), 0.0f, 1.0f);
                const auto code = static_cast<std::uint8_t>(std::lround(255.0f * value));
                brightest = std::max(brightest, code);
                *target++ = code;
            }
            *target++ = 255;
        }
    }

    result.description = std::to_string(width) + "×" + std::to_string(height) +
                         (layout.fixedPoint ? "  2.14 RGB" : "  8-bit RGB") + "  " + source;
    result.visible = brightest > 5;
    if (!result.visible)
        result.description += " (near black)";
    return result;
}

PreviewProbeInput CaptureInput(IAVXEffectContext* context, acfUInt32 index, acfUInt32 frame, avx2Field field)
{
    ACFPtr<IAVXImageInput> input;
    const auto lookup = context->GetInput(index, AVXInputTypeID_Image, IID_PPV_ARG(IAVXImageInput, &input));
    if (ACFFAILED(lookup) || !input) {
        char message[100];
        std::snprintf(message, sizeof(message), "GetInput returned 0x%08x", static_cast<unsigned>(lookup));
        return {message};
    }

    ACFPtr<IAVXImage> image;
    const auto read = input->GetImage(frame, field, kAVXFieldMode_Progressive, &image);
    PreviewProbeInput regular;
    if (ACFSUCCEEDED(read) && image) {
        regular = SampleInput(image, "GetImage");
        if (regular.visible)
            return regular;
    } else {
        char message[100];
        std::snprintf(message, sizeof(message), "GetImage returned 0x%08x", static_cast<unsigned>(read));
        regular.description = message;
    }

    // Some hosts provide the media through the full-resolution AVX input interface.
    ACFPtr<IAVXImageInput_v2> fullInput;
    if (ACFSUCCEEDED(input->QueryInterface(IID_PPV_ARG(IAVXImageInput_v2, &fullInput))) && fullInput) {
        ACFPtr<IAVXImage> full;
        const auto fullRead = fullInput->GetImageFullRez(frame, field, kAVXFieldMode_Progressive, &full);
        if (ACFSUCCEEDED(fullRead) && full) {
            auto fallback = SampleInput(full, "FullRez");
            if (fallback.visible || regular.rgba.empty())
                return fallback;
        }
    }
    return regular;
}
#endif

void WriteComponent(avx2Pixel8* pixel, acfSInt32 channel, float code, const RgbLayout& layout)
{
    if (layout.fixedPoint) {
        const auto low = static_cast<float>(std::numeric_limits<avx2Pixel2_14>::min());
        const auto high = static_cast<float>(std::numeric_limits<avx2Pixel2_14>::max());
        const auto sample = static_cast<avx2Pixel2_14>(std::lround(std::fmax(low, std::fmin(high, code))));
        std::memcpy(pixel + channel * sizeof(sample), &sample, sizeof(sample));
    } else {
        pixel[channel] = static_cast<avx2Pixel8>(std::lround(std::fmax(0.0f, std::fmin(255.0f, code))));
    }
}

void WriteColor(avx2Pixel8* pixel, acfSInt32 channel, float value, const RgbLayout& layout)
{
    WriteComponent(pixel, channel, layout.black + layout.colorSpan * value, layout);
}

void WriteAlpha(avx2Pixel8* pixel, float value, const RgbLayout& layout)
{
    if (layout.alpha >= 0)
        WriteComponent(pixel, layout.alpha, value * (layout.fixedPoint ? kFixedPointOne : 255.0f), layout);
}

float ToLinear(float value)
{
    return value < 0.081f ? value / 4.5f : std::pow((value + 0.099f) / 1.099f, 1.0f / 0.45f);
}

float ToVideo(float value)
{
    return value < 0.018f ? 4.5f * value : 1.099f * std::pow(value, 0.45f) - 0.099f;
}

#ifndef CK_SYNTHETIC_MATTE
using namespace corridor_key_mac;

template <typename T>
T ReadParameter(IAVXEffectContext* effect, acfDouble time, const acfUID& id,
                const acfUID& type, T fallback)
{
    ACFPtr<IAVXParameter> parameter;
    if (ACFFAILED(effect->GetParameter(id, IID_PPV_ARG(IAVXParameter, &parameter))) || !parameter)
        return fallback;

    T value = fallback;
    return ACFSUCCEEDED(parameter->GetValue(time, type, &value, sizeof(value))) ? value : fallback;
}

class Despill {
public:
    Despill(float strength, const std::array<float, 3>& color) : strength_(strength)
    {
        const auto [low, high] = std::minmax_element(color.begin(), color.end());
        if (*low == *high || strength_ == 0.0f) {
            strength_ = 0.0f;
            return;
        }

        // Match CorridorKey's Average method exactly for a single dominant channel.
        const auto channel = static_cast<int>(high - color.begin());
        if (color[(channel + 1) % 3] == color[(channel + 2) % 3]) {
            primary_ = channel;
            return;
        }

        // Remove the neutral component of the picked color. Scale the chroma
        // direction to the same length as the Average method's (-0.5, 1, -0.5).
        // Normalize by chroma first to keep the projection stable near gray.
        const float chroma = *high - *low;
        for (int i = 0; i < 3; ++i)
            direction_[i] = (color[i] - *low) / chroma;
        const float mean = (direction_[0] + direction_[1] + direction_[2]) / 3.0f;
        float normSquared = 0.0f;
        for (float& component : direction_) {
            component -= mean;
            normSquared += component * component;
        }
        const float scale = std::sqrt(1.5f / normSquared);
        for (float& component : direction_)
            component *= scale;
    }

    std::array<float, 3> Apply(const float* rgb) const
    {
        std::array<float, 3> result = {rgb[0], rgb[1], rgb[2]};
        if (strength_ == 0.0f)
            return result;

        if (primary_ >= 0) {
            const int next = (primary_ + 1) % 3;
            const int other = (primary_ + 2) % 3;
            const float spill = std::max(0.0f, rgb[primary_] - (rgb[next] + rgb[other]) * 0.5f);
            result[primary_] -= spill;
            result[next] += spill * 0.5f;
            result[other] += spill * 0.5f;
        } else {
            const float spill = std::max(0.0f, (rgb[0] - rgb[1]) * direction_[0] +
                                              (rgb[2] - rgb[1]) * direction_[2]);
            for (int i = 0; i < 3; ++i)
                result[i] -= spill * direction_[i];
        }

        if (strength_ < 1.0f) {
            for (int i = 0; i < 3; ++i)
                result[i] = rgb[i] * (1.0f - strength_) + result[i] * strength_;
        }
        return result;
    }

private:
    float strength_;
    int primary_ = -1;
    std::array<float, 3> direction_{};
};

Despill ReadDespill(IAVXEffectContext* effect, acfDouble time)
{
    const auto amount = ReadParameter(effect, time, PARAMETER_Despill_GUID, ACFTypeID_UInt32,
                                      kDefaultDespillAmount);
    const acfUID ids[] = {PARAMETER_MacDespillRed, PARAMETER_MacDespillGreen, PARAMETER_MacDespillBlue};
    std::array<float, 3> color;
    for (std::size_t i = 0; i < color.size(); ++i) {
        const double fallback = static_cast<double>(kDefaultDespillColor[i]) / kColorCodeMax;
        const double value = ReadParameter(effect, time, ids[i], ACFTypeID_Double, fallback);
        color[i] = ToLinear(static_cast<float>(std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback));
    }
    return Despill(std::min(amount, acfUInt32{100}) / 100.0f, color);
}

std::mutex workerMutex;
std::unique_ptr<MacFrameWorker> worker;

void ReportRenderError(const char* message)
{
    os_log_error(OS_LOG_DEFAULT, "CorridorKey render failed: %{public}s", message);
    std::fprintf(stderr, "CorridorKey render failed: %s\n", message);

    // Avid can return to the timeline without showing an AVX render error.
    // Rate-limit the non-blocking notice if the host tries several frames.
    using Clock = std::chrono::steady_clock;
    static auto nextNotice = Clock::time_point::min();
    const auto now = Clock::now();
    if (now < nextNotice)
        return;
    nextNotice = now + std::chrono::seconds(10);

    const std::string_view detail(message);
    const auto help = detail.find("missing runtime asset") == 0
        ? CFSTR("Required model files are missing. Restore the files and render again. See worker.log for details.")
        : CFSTR("The local worker could not finish the frame. Try rendering again. See worker.log or macOS Console for details.");
    CFUserNotificationDisplayNotice(10, kCFUserNotificationNoteAlertLevel, nullptr,
                                    nullptr, nullptr, CFSTR("CorridorKey render failed"),
                                    help, CFSTR("OK"));
}

ACFRESULT RunKeyer(IAVXEffectContext* effect, IAVXImage* foreground, const RgbLayout& layout,
                   std::vector<float>& rgba)
{
    const auto width = foreground->GetWidth();
    const auto height = foreground->GetHeight();
    if (width > 1920 || height > 1080 || foreground->GetFieldMode() != kAVXFieldMode_Progressive)
        return ACF_E_INVALIDARG;

    std::vector<float> rgb(static_cast<std::size_t>(width) * height * 3);
    auto* destination = rgb.data();
    for (acfUInt32 y = 0; y < height; ++y) {
        const auto* source = foreground->GetLinePtr(y, 0);
        if (!source)
            return ACF_E_FAIL;
        for (acfUInt32 x = 0; x < width; ++x) {
            for (auto channel : layout.rgb) {
                const auto color = ReadColor(source, channel, layout);
                *destination++ = ToLinear(std::clamp(color, 0.0f, 1.0f));
            }
            source += layout.stride;
        }
    }

    const std::lock_guard guard(workerMutex);
    try {
        if (!worker)
            worker = std::make_unique<MacFrameWorker>();
        rgba = worker->Process(width, height, rgb, [effect] {
            return effect->GetInterruptFlag() != 0;
        });
    } catch (const MacFrameWorker::Interrupted&) {
        return ACF_E_ABORT;
    } catch (const std::exception& error) {
        ReportRenderError(error.what());
        return ACF_E_FAIL;
    }
    return ACF_OK;
}

ACFRESULT CopyForeground(IAVXImage* source, IAVXImage* output,
                         const RgbLayout& inputLayout, const RgbLayout& outputLayout)
{
    for (acfUInt32 y = 0; y < output->GetHeight(); ++y) {
        const auto* src = source->GetLinePtr(y, 0);
        auto* dst = output->GetLinePtr(y, 0);
        if (!src || !dst)
            return ACF_E_FAIL;
        for (acfUInt32 x = 0; x < output->GetWidth(); ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                const auto value = ReadColor(src, inputLayout.rgb[channel], inputLayout);
                WriteColor(dst, outputLayout.rgb[channel], value, outputLayout);
            }
            WriteAlpha(dst, 1.0f, outputLayout);
            src += inputLayout.stride;
            dst += outputLayout.stride;
        }
    }
    return ACF_OK;
}

ACFRESULT WriteMatte(IAVXImage* output, const RgbLayout& layout, const float* rgba)
{
    for (acfUInt32 y = 0; y < output->GetHeight(); ++y) {
        auto* dst = output->GetLinePtr(y, 0);
        if (!dst)
            return ACF_E_FAIL;
        for (acfUInt32 x = 0; x < output->GetWidth(); ++x) {
            const float alpha = std::clamp(rgba[3], 0.0f, 1.0f);
            for (auto channel : layout.rgb)
                WriteColor(dst, channel, alpha, layout);
            WriteAlpha(dst, 1.0f, layout);
            dst += layout.stride;
            rgba += 4;
        }
    }
    return ACF_OK;
}
#endif

}

#ifdef CK_PREVIEW_PROBE
ACF_DEFINE_FACTORY(CorridorKeyPreviewProbe)

ACFMETHODIMP CorridorKeyPreviewProbe::InternalQueryInterface(const acfIID& iid, void** result)
{
    if (!result)
        return ACF_E_POINTER;
    if (iid == IID_IAVXModalUI) {
        *result = static_cast<IAVXModalUI*>(this);
        AddRef();
        return ACF_OK;
    }
    return CACFUnknown::InternalQueryInterface(iid, result);
}

ACFMETHODIMP CorridorKeyPreviewProbe::DoDialog(IAVXEffectContext* effect, IAVXFrameContext* frame,
                                               IAVXRenderContext* render)
{
    BEGIN_ACF_METHOD
    if (!effect || !frame || !render)
        return ACF_E_POINTER;

    const auto frameNumber = frame->GetFrameNumber();
    const auto field = render->GetField();
    const std::array<PreviewProbeInput, 2> inputs = {
        CaptureInput(effect, kBackgroundInput, frameNumber, field),
        CaptureInput(effect, kForegroundInput, frameNumber, field),
    };
    const bool distinct = inputs[0].visible && inputs[1].visible && inputs[0].rgba != inputs[1].rgba;
    os_log(OS_LOG_DEFAULT, "CorridorKey preview probe frame %u, %u inputs, main thread %d, V1: %{public}s, V2: %{public}s, distinct %d",
           frameNumber, effect->GetNumInputs(), pthread_main_np(), inputs[0].description.c_str(),
           inputs[1].description.c_str(), static_cast<int>(distinct));
    ShowPreviewProbe(frameNumber, inputs, distinct);
    END_ACF_METHOD
}
#endif

#ifndef CK_SYNTHETIC_MATTE
ACFAPI ACFShutdown(IACFUnknown*)
{
    BEGIN_ACF_METHOD
    const std::lock_guard guard(workerMutex);
    worker.reset();
    END_ACF_METHOD
}
#endif

CCorridorKey_Effect::CCorridorKey_Effect(IACFUnknown* controllingUnknown)
    : CACFUnknown(controllingUnknown)
{
}

ACFMETHODIMP CCorridorKey_Effect::InitializeInstance(IACFUnknown* host)
{
    return host ? ACF_OK : ACF_E_POINTER;
}

ACFMETHODIMP CCorridorKey_Effect::InternalQueryInterface(const acfIID& iid, void** result)
{
    if (!result)
        return ACF_E_POINTER;
    if (iid == IID_IAVXEffect) {
        *result = static_cast<IAVXEffect*>(this);
        AddRef();
        return ACF_OK;
    }
    return CACFUnknown::InternalQueryInterface(iid, result);
}

ACFMETHODIMP CCorridorKey_Effect::GetEffectAttribute(
    const acfUID& id, IACFValue* value, IAVXEffectContext*, void*, void*,
    IAVXFrameContext*, void*)
{
    if (id != AVXATTR_Effect_Realtime && id != AVXATTR_Effect_RenderSingleThreaded)
        return ACF_E_NOTIMPL;
    if (!value)
        return ACF_E_POINTER;
    const acfBool enabled = id == AVXATTR_Effect_RenderSingleThreaded ? kACFTrue : kACFFalse;
    value->Clear();
    return value->InitializeValue(ACFTypeID_Boolean, &enabled, sizeof(enabled));
}

ACFMETHODIMP CCorridorKey_Effect::Render(
    IAVXEffectContext* effect, void*, void*, IAVXFrameContext* frame, void*,
    IAVXRenderContext* render, IAVXOutput* output)
{
    BEGIN_ACF_METHOD
    if (!effect || !frame || !render || !output)
        return ACF_E_POINTER;
    ACFPtr<IAVXImage> image;
    acfcheck(output->GetOutput(0, AVXOutputTypeID_Image, IID_PPV_ARG(IAVXImage, &image)));
    if (!image)
        return ACF_E_DATANOTAVAILABLE;
    RgbLayout out{};
    if (!ReadLayout(image, out))
        return ACF_E_FAIL;

    const auto width = image->GetWidth();
    const auto height = image->GetHeight();
    if (width == 0 || height == 0)
        return ACF_E_DATANOTAVAILABLE;
#ifndef CK_SYNTHETIC_MATTE
    ACFPtr<IAVXImage> plate;
    acfcheck(GetInputImage(effect, kForegroundInput, frame->GetFrameNumber(), render->GetField(),
                           image->GetFieldMode(), plate));
    RgbLayout plateLayout{};
    if (!plate || plate->GetWidth() != width || plate->GetHeight() != height ||
        !ReadLayout(plate, plateLayout))
        return ACF_E_DATANOTAVAILABLE;
    const auto renderMode = effect->GetRenderMode();
    os_log_debug(OS_LOG_DEFAULT, "CorridorKey render mode %d, %ux%u, foreground %{public}s, output %{public}s",
                 static_cast<int>(renderMode), width, height,
                 plateLayout.fixedPoint ? "2.14 RGB" : "8-bit RGB",
                 out.fixedPoint ? "2.14 RGB" : "8-bit RGB");
    if (renderMode != kAVXRenderMode_MultipleFrame)
        return CopyForeground(plate, image, plateLayout, out);

    const acfDouble time = render->GetTime();
    const auto view = static_cast<View>(ReadParameter(effect, time, PARAMETER_MacView, ACFTypeID_UInt32,
                                                      static_cast<acfUInt32>(View::Composite)));
    if (view == View::Original)
        return CopyForeground(plate, image, plateLayout, out);
    if (view == View::Matte) {
        std::vector<float> keyed;
        acfcheck(RunKeyer(effect, plate, plateLayout, keyed));
        return WriteMatte(image, out, keyed.data());
    }
#endif
#ifdef CK_EXPLICIT_COMPOSITE
    ACFPtr<IAVXImage> background;
    acfcheck(GetInputImage(effect, kBackgroundInput, frame->GetFrameNumber(), render->GetField(),
                           image->GetFieldMode(), background));
    RgbLayout bg{};
    if (!background || background->GetWidth() != width || background->GetHeight() != height ||
        !ReadLayout(background, bg))
        return ACF_E_DATANOTAVAILABLE;
#ifndef CK_SYNTHETIC_MATTE
    os_log_debug(OS_LOG_DEFAULT, "CorridorKey background %{public}s", bg.fixedPoint ? "2.14 RGB" : "8-bit RGB");
#endif
#else
    if (out.alpha < 0)
        return ACF_E_FAIL;
#endif
#ifdef CK_SYNTHETIC_MATTE
    constexpr std::array<float, 3> foreground = {0.96f, 0.48f, 0.13f};
#else
    std::vector<float> keyed;
    acfcheck(RunKeyer(effect, plate, plateLayout, keyed));
    const float* foreground = keyed.data();
    const auto despill = ReadDespill(effect, time);
#endif
    for (acfUInt32 y = 0; y < height; ++y) {
        auto* dst = image->GetLinePtr(y, 0);
#ifdef CK_EXPLICIT_COMPOSITE
        const auto* src = background->GetLinePtr(y, 0);
        if (!src)
            return ACF_E_FAIL;
#endif
        if (!dst)
            return ACF_E_FAIL;
        for (acfUInt32 x = 0; x < width; ++x) {
#ifdef CK_SYNTHETIC_MATTE
            const float alpha = 0.5f * static_cast<float>((3 * x) / width);
            const auto linear = [&foreground](int channel) { return ToLinear(foreground[channel]); };
#else
            const float alpha = std::clamp(foreground[3], 0.0f, 1.0f);
            const auto color = despill.Apply(foreground);
            const auto linear = [&color](int channel) { return color[channel]; };
#endif
            for (int channel = 0; channel < 3; ++channel) {
#ifdef CK_EXPLICIT_COMPOSITE
                const float underneath = ReadColor(src, bg.rgb[channel], bg);
                const float color = alpha == 0.0f ? underneath :
                    ToVideo(alpha * linear(channel) + (1.0f - alpha) * ToLinear(underneath));
#else
                const float color = ToVideo(linear(channel));
#endif
                WriteColor(dst, out.rgb[channel], color, out);
            }
#ifdef CK_EXPLICIT_COMPOSITE
            WriteAlpha(dst, 1.0f, out);
#else
            WriteAlpha(dst, alpha, out);
#endif
            dst += out.stride;
#ifdef CK_EXPLICIT_COMPOSITE
            src += bg.stride;
#endif
#ifndef CK_SYNTHETIC_MATTE
            foreground += 4;
#endif
        }
    }
    END_ACF_METHOD
}
