#define INITACFIDS

#include "avx2api.h"
#include "CACFClassFactory.h"
#include "ACFPtr.h"
#include "acfextras.h"

#ifdef __APPLE__
#include "CorridorKeyEffect_mac.h"
#else
#include "CorridorKeyEffect.h"
#endif
#include "CorridorKeyUIDs.h"
#ifdef CK_PREVIEW_PROBE
#include "PreviewProbe_mac.h"
#endif

static const acfUInt32 kEffectMajorVersion = 2;
static const acfUInt32 kEffectMinorVersion = 0;

static const acfWChar* sPluginVendor = L"CorridorKey Community";
#ifdef CK_PREVIEW_PROBE
static const acfUID& kPluginId = MID_PreviewProbe;
static const acfUID& kEffectId = EID_PreviewProbe_Effect;
static const acfCLSID& kEffectClass = CLSID_PreviewProbe_Effect;
static const acfUInt32 kTotalPluginComponents = 2;
static const acfWChar* sPluginName = L"CorridorKeyPreviewProbe.avx";
#else
static const acfUID& kPluginId = MID_CorridorKey_Plugin;
static const acfUID& kEffectId = EID_CorridorKey_Effect;
static const acfCLSID& kEffectClass = CLSID_CorridorKey_Effect;
static const acfUInt32 kTotalPluginComponents = 1;
static const acfWChar* sPluginName = L"CorridorKey.avx";
#endif
#ifdef __APPLE__
#ifdef CK_PREVIEW_PROBE
static const acfWChar* sPluginDescription = L"Checks both timeline inputs in the AVX2 dialog.";
static const acfWChar* sComponentName = L"CorridorKey Timeline Input Test";
#elif defined(CK_SYNTHETIC_MATTE)
static const acfWChar* sPluginDescription = L"CorridorKey 2.0 timeline alpha validation.";
#ifdef CK_EXPLICIT_COMPOSITE
static const acfWChar* sComponentName = L"CorridorKey Composite Test";
#else
static const acfWChar* sComponentName = L"CorridorKey Alpha Test";
#endif
#else
static const acfWChar* sPluginDescription = L"CorridorKey 2.0 neural green and blue screen keyer.";
static const acfWChar* sComponentName = L"Corridor Key 2.0";
#endif
#ifdef CK_EXPLICIT_COMPOSITE
static const acfWChar* sCategoryName = L"Key";
#else
static const acfWChar* sCategoryName = L"Image";
#endif
#else
static const acfWChar* sPluginDescription =
    L"Corridor Key 2.0 neural green/blue screen keyer for Avid Media Composer. "
    L"One-click keying, Time Awareness, Despill/Unmix, Alpha Hint, multi-output.";
static const acfWChar* sComponentName = L"Corridor Key 2.0";
// Image = segment filter (same as FlipFlop/Fade). Key category made MC wire inputs like a keyer.
static const acfWChar* sCategoryName = L"Image";
#endif

#if !defined(__APPLE__) || !defined(CK_SYNTHETIC_MATTE)
static void AddUInt32Slider(
    IACFComponentFactory* pFactory,
    IAVXEffectDefinition* pEffectDef,
    const acfUID& paramGuid,
    const acfUID& layoutGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfUInt32 defVal,
    acfUInt32 minVal,
    acfUInt32 maxVal)
{
    ACFPtr<IAVXParameterDefinition> pParam;
    acfcheck(pFactory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                       IID_IAVXParameterDefinition, (void**)&pParam));
    if (!pParam)
        return;
    acfcheck(pParam->InitializeParameterDefinition(
        paramGuid, name, desc, ACFTypeID_UInt32, kAVXKeyFrameBehavior_Static));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32, &defVal, sizeof(defVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Minimum, ACFTypeID_UInt32, &minVal, sizeof(minVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Maximum, ACFTypeID_UInt32, &maxVal, sizeof(maxVal)));
    acfcheck(pEffectDef->AddParameter(pParam));

    acfUID viewId = ACF_WIDGET_SLIDER;
    ACFPtr<IAVXLayoutDefinition> pLayout;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pLayout));
    if (!pLayout)
        return;
    acfcheck(pLayout->InitializeLayoutDefinition(layoutGuid, name, desc, &viewId, pParam));
    acfcheck(pEffectDef->AddLayoutDefinition(pLayout));
}

#ifndef __APPLE__
static void AddCheckbox(
    IACFComponentFactory* pFactory,
    IAVXEffectDefinition* pEffectDef,
    const acfUID& paramGuid,
    const acfUID& layoutGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfBool defVal)
{
    ACFPtr<IAVXParameterDefinition> pParam;
    acfcheck(pFactory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                       IID_IAVXParameterDefinition, (void**)&pParam));
    if (!pParam)
        return;
    acfcheck(pParam->InitializeParameterDefinition(
        paramGuid, name, desc, ACFTypeID_Boolean, kAVXKeyFrameBehavior_Static));
    acfBool minVal = kACFFalse;
    acfBool maxVal = kACFTrue;
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_Boolean, &defVal, sizeof(defVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Minimum, ACFTypeID_Boolean, &minVal, sizeof(minVal)));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Maximum, ACFTypeID_Boolean, &maxVal, sizeof(maxVal)));
    acfcheck(pEffectDef->AddParameter(pParam));

    acfUID viewId = ACF_WIDGET_CHECKBOX;
    ACFPtr<IAVXLayoutDefinition> pLayout;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pLayout));
    if (!pLayout)
        return;
    acfcheck(pLayout->InitializeLayoutDefinition(layoutGuid, name, desc, &viewId, pParam));
    acfcheck(pEffectDef->AddLayoutDefinition(pLayout));
}
#endif

static ACFPtr<IAVXLayoutDefinition> MakeListItem(
    IACFComponentFactory* pFactory,
    const acfUID& itemGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfUInt32 value)
{
    ACFPtr<IAVXLayoutDefinition> pItem;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pItem));
    if (!pItem)
        return pItem;
    acfUID viewId = ACF_WIDGET_LISTITEM;
    acfcheck(pItem->InitializeLayoutDefinition(itemGuid, name, desc, &viewId));
    acfcheck(pItem->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32, &value, sizeof(value)));
    return pItem;
}

static void AddListBox(
    IACFComponentFactory* pFactory,
    IAVXEffectDefinition* pEffectDef,
    const acfUID& paramGuid,
    const acfUID& layoutGuid,
    const acfWChar* name,
    const acfWChar* desc,
    acfUInt32 defVal,
    const acfUID* itemGuids,
    const acfWChar** itemNames,
    const acfUInt32* itemValues,
    acfUInt32 itemCount)
{
    ACFPtr<IAVXParameterDefinition> pParam;
    acfcheck(pFactory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                       IID_IAVXParameterDefinition, (void**)&pParam));
    if (!pParam)
        return;
    acfcheck(pParam->InitializeParameterDefinition(
        paramGuid, name, desc, ACFTypeID_UInt32, kAVXKeyFrameBehavior_Static));
    acfcheck(pParam->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32, &defVal, sizeof(defVal)));
    acfcheck(pEffectDef->AddParameter(pParam));

    acfUID viewId = ACF_WIDGET_LISTBOX;
    ACFPtr<IAVXLayoutDefinition> pLayout;
    acfcheck(pFactory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                       IID_IAVXLayoutDefinition, (void**)&pLayout));
    if (!pLayout)
        return;
    acfcheck(pLayout->InitializeLayoutDefinition(layoutGuid, name, desc, &viewId, pParam));

    for (acfUInt32 i = 0; i < itemCount; ++i) {
        ACFPtr<IAVXLayoutDefinition> pItem =
            MakeListItem(pFactory, itemGuids[i], itemNames[i], itemNames[i], itemValues[i]);
        if (pItem)
            acfcheck(pLayout->AddLayoutDefinition(pItem));
    }

    acfcheck(pEffectDef->AddLayoutDefinition(pLayout));
}

#ifdef __APPLE__
static void AddDespillColor(IACFComponentFactory* factory, IAVXEffectDefinition* effect)
{
    using namespace corridor_key_mac;
    const acfWChar* name = L"Despill Color";
    const acfWChar* description = L"Color cast to remove from the keyed foreground";
    const acfUID parameterIds[] = { PARAMETER_MacDespillRed, PARAMETER_MacDespillGreen, PARAMETER_MacDespillBlue };
    const acfUID layoutIds[] = { LAYOUT_MacDespillRed, LAYOUT_MacDespillGreen, LAYOUT_MacDespillBlue };
    const acfWChar* channelNames[] = { L"Red", L"Green", L"Blue" };
    const acfUInt32 min = 0;
    const acfUInt32 increment = 1;
    std::array<ACFPtr<IAVXParameterDefinition>, 3> channels;
    IAVXParameterDefinition* components[3];

    // The native picker binds three double channels remapped to 0–255 in the UI.
    for (std::size_t i = 0; i < channels.size(); ++i) {
        auto& channel = channels[i];
        acfcheck(factory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                          IID_IAVXParameterDefinition, (void**)&channel));
        acfcheck(channel->InitializeParameterDefinition(parameterIds[i], channelNames[i], description,
                                                        ACFTypeID_Double, kAVXKeyFrameBehavior_Animateable));
        acfcheck(channel->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_UInt32,
                                          &kDefaultDespillColor[i], sizeof(acfUInt32)));
        acfcheck(channel->DefineAttribute(ACFATTR_ParamDefinition_Minimum, ACFTypeID_UInt32, &min, sizeof(min)));
        acfcheck(channel->DefineAttribute(ACFATTR_ParamDefinition_Maximum, ACFTypeID_UInt32,
                                          &kColorCodeMax, sizeof(kColorCodeMax)));
        acfcheck(channel->DefineAttribute(ACFATTR_ParamDefinition_Increment, ACFTypeID_UInt32,
                                          &increment, sizeof(increment)));
        acfcheck(channel->DefineAttribute(ACFATTR_ParamDefinition_UIRemap, ACFTypeID_UInt32,
                                          &kColorCodeMax, sizeof(kColorCodeMax)));
        components[i] = channel;
    }

    ACFPtr<IAVXParameterDefinition> color;
    acfcheck(factory->CreateComponent(ACFCompID_ParameterDefinition, 0,
                                      IID_IAVXParameterDefinition, (void**)&color));
    acfcheck(color->InitializeParameterDefinition(PARAMETER_MacDespillColor, name, description,
                                                  ACFTypeID_ColorRGB, kAVXKeyFrameBehavior_Animateable,
                                                  kAVXCoordinateBehavior_None, 3, components));
    ACFPtr<IAVXLayoutDefinition> picker;
    acfcheck(factory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                      IID_IAVXLayoutDefinition, (void**)&picker));
    acfcheck(picker->InitializeLayoutDefinition(LAYOUT_MacDespillColor, name, description,
                                               &ACF_WIDGET_COLORPICKER, color));
    for (std::size_t i = 0; i < channels.size(); ++i) {
        ACFPtr<IAVXLayoutDefinition> slider;
        acfcheck(factory->CreateComponent(ACFCompID_LayoutDefinition, 0,
                                          IID_IAVXLayoutDefinition, (void**)&slider));
        acfcheck(slider->InitializeLayoutDefinition(layoutIds[i], channelNames[i], description,
                                                    &ACF_WIDGET_SLIDER, channels[i]));
        acfcheck(picker->AddLayoutDefinition(slider));
    }

    const avx2RGB defaultColor = {kDefaultDespillColor[0], kDefaultDespillColor[1], kDefaultDespillColor[2]};
    const avx2RGB minColor = {0, 0, 0};
    const avx2RGB maxColor = {kColorCodeMax, kColorCodeMax, kColorCodeMax};
    acfcheck(picker->DefineAttribute(ACFATTR_ParamDefinition_Default, ACFTypeID_ColorRGB,
                                     &defaultColor, sizeof(defaultColor)));
    acfcheck(picker->DefineAttribute(ACFATTR_ParamDefinition_Minimum, ACFTypeID_ColorRGB,
                                     &minColor, sizeof(minColor)));
    acfcheck(picker->DefineAttribute(ACFATTR_ParamDefinition_Maximum, ACFTypeID_ColorRGB,
                                     &maxColor, sizeof(maxColor)));
    acfcheck(picker->DefineAttribute(ACFATTR_ParamDefinition_UIRemap, ACFTypeID_ColorRGB,
                                     &maxColor, sizeof(maxColor)));
    acfcheck(effect->AddLayoutDefinition(picker));
}
#endif
#endif

extern "C" ACFAPI ACFRegisterPlugin(IACFUnknown* pUnkHost, IACFPluginDefinition** ppPluginDefinition)
{
    BEGIN_ACF_METHOD
    if (!pUnkHost || !ppPluginDefinition)
        return ACF_E_POINTER;
    *ppPluginDefinition = 0;

    ACFPtr<IACFComponentFactory> pFactory;
    acfcheck(pUnkHost->QueryInterface(IID_IACFComponentFactory, (void**)&pFactory));

    ACFPtr<IACFPluginDefinition> pPluginDef;
    acfcheck(pFactory->CreateComponent(ACFCompID_PluginDefinition, 0, IID_IACFPluginDefinition, (void**)&pPluginDef));
    acfcheck(pPluginDef->InitializePlugin(
        kPluginId,
        kEffectMajorVersion,
        kEffectMinorVersion,
        sPluginName,
        AVXVendorID_CorridorKey,
        sPluginVendor,
        kTotalPluginComponents,
        kACFTrue));

    acfcheck(pPluginDef->DefineAttribute(
        ACFATTR_Definition_Description,
        ACFTypeID_UnicodeString,
        sPluginDescription,
        ACF_SIZE_UNK));

    *ppPluginDefinition = pPluginDef.detach();
    END_ACF_METHOD
}

extern "C" ACFAPI ACFRegisterComponent(
    IACFUnknown* pUnkHost,
    acfUInt32 index,
    IACFComponentDefinition** ppComponentDefinition)
{
    BEGIN_ACF_METHOD

    if (!pUnkHost || !ppComponentDefinition)
        return ACF_E_POINTER;
    if (kTotalPluginComponents <= index)
        return ACF_E_INVALIDARG;

    *ppComponentDefinition = 0;

    ACFPtr<IACFComponentFactory> pFactory;
    acfcheck(pUnkHost->QueryInterface(IID_IACFComponentFactory, (void**)&pFactory));

#ifdef CK_PREVIEW_PROBE
    if (index == 1) {
        ACFPtr<IACFComponentDefinition> dialog;
        acfcheck(pFactory->CreateComponent(ACFCompID_ComponentDefinition, 0,
                                          IID_IACFComponentDefinition, (void**)&dialog));
        acfcheck(dialog->InitializeComponent(EID_PreviewProbe_Dialog, ACFCompType_ModalUI,
            kEffectMajorVersion, kEffectMinorVersion, CLSID_PreviewProbe_Dialog, L"Check Timeline Inputs"));
        *ppComponentDefinition = dialog.detach();
        return ACF_OK;
    }
#endif

    ACFPtr<IAVXEffectDefinition> pEffectDef;
    acfcheck(pFactory->CreateComponent(ACFCompID_EffectDefinition, 0, IID_IAVXEffectDefinition, (void**)&pEffectDef));
    acfcheck(pEffectDef->InitializeComponent(
        kEffectId,
        ACFCompType_ImageEffect,
        kEffectMajorVersion,
        kEffectMinorVersion,
        kEffectClass,
        sComponentName));

    acfcheck(pEffectDef->DefineAttribute(ACFATTR_Effect_Category, ACFTypeID_UnicodeString, sCategoryName, ACF_SIZE_UNK));
#ifdef CK_PREVIEW_PROBE
    acfcheck(pEffectDef->DefineAttribute(AVXATTR_Effect_ModalUI, ACFTypeID_UID,
                                       &EID_PreviewProbe_Dialog, sizeof(EID_PreviewProbe_Dialog)));
#endif

#ifdef __APPLE__
#ifdef CK_EXPLICIT_COMPOSITE
    acfcheck(pEffectDef->AddInput(ACFLayerID_Foreground, L"Foreground", kAVXDataType_Color));
    acfcheck(pEffectDef->AddInput(ACFLayerID_Background, L"Background", kAVXDataType_Color));
#else
    acfcheck(pEffectDef->AddInput(ACFLayerID_Input, L"Input", kAVXDataType_Color));
#endif
    acfcheck(pEffectDef->AddOutput(ACFLayerID_Output, L"Output", kAVXDataType_Color));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_SHORT_2_14));
#ifndef CK_SYNTHETIC_MATTE
    using namespace corridor_key_mac;
    const acfUID viewItems[] = { LISTITEM_MacComposite, LISTITEM_MacMatte, LISTITEM_MacOriginal };
    const acfWChar* viewNames[] = { L"Composite", L"Matte", L"Original" };
    const acfUInt32 viewValues[] = { static_cast<acfUInt32>(View::Composite),
                                    static_cast<acfUInt32>(View::Matte),
                                    static_cast<acfUInt32>(View::Original) };
    AddListBox(pFactory, pEffectDef, PARAMETER_MacView, LAYOUT_MacView,
               L"View", L"Select the composite, matte, or original foreground to render",
               viewValues[0], viewItems, viewNames, viewValues, 3);
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_Despill_GUID, LAYOUT_Despill_GUID,
                    L"Despill Amount", L"Strength of foreground spill suppression (0–100%)",
                    kDefaultDespillAmount, 0, 100);
    AddDespillColor(pFactory, pEffectDef);
#endif
#else
    // ONE input only. Empirically: AddOptionalInput / AddInput for a 2nd layer makes
    // Media Composer deliver flat video-black (Y=16) on the primary for single-clip
    // applies — SupportNoInputData does not prevent that. Alpha Hint is Mode=External
    // using the clip's own alpha (nest a matte into alpha, or use an RGBA source).
    acfcheck(pEffectDef->AddInput(ACFLayerID_Input, L"Input", kAVXDataType_Color));
    acfcheck(pEffectDef->AddOutput(ACFLayerID_Output, L"Output", kAVXDataType_Color));

    // Formats Media Composer commonly delivers (RGB + YUV, 8/10/16-bit).
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_RGBA_Interleaved, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGR_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_RGB_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_BGRA_Interleaved, kAVXComponentType_USHORT));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_ARGB_Interleaved, kAVXComponentType_USHORT));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_CbYCrY_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_YCbYCr_NoA, kAVXComponentType_UCHAR));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_CbYCrY_NoA, kAVXComponentType_SHORT_2_14));
    acfcheck(pEffectDef->AddSupportedFormat(kAVXComponentOrder_CbYCrY_NoA, kAVXComponentType_USHORT_10_6));

    // Mode dropdown
    {
        static const acfUID guids[] = { LISTITEM_Mode_Auto, LISTITEM_Mode_External };
        static const acfWChar* names[] = { L"Auto", L"External Hint (FG alpha)" };
        static const acfUInt32 values[] = { 0, 1 };
        AddListBox(pFactory, pEffectDef, PARAMETER_Mode_GUID, LAYOUT_Mode_GUID,
                   L"Mode",
                   L"Auto = one-click key. External = use this clip's alpha channel as Alpha Hint.",
                   0, guids, names, values, 2);
    }

    // Output dropdown
    {
        static const acfUID guids[] = {
            LISTITEM_Out_Processed, LISTITEM_Out_Matte,
            LISTITEM_Out_Foreground, LISTITEM_Out_Checker
        };
        static const acfWChar* names[] = {
            L"Processed", L"Matte", L"Foreground", L"Checkerboard"
        };
        static const acfUInt32 values[] = { 0, 1, 2, 3 };
        AddListBox(pFactory, pEffectDef, PARAMETER_Output_GUID, LAYOUT_Output_GUID,
                   L"Output", L"Processed / Matte / Foreground / Checkerboard",
                   0, guids, names, values, 4);
    }

    // Despill / Unmix strength (0–100%)
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_Despill_GUID, LAYOUT_Despill_GUID,
                    L"Despill / Unmix", L"Spill removal / unmix strength (0-100%)",
                    50, 0, 100);

    // Colorspace dropdown
    {
        static const acfUID guids[] = { LISTITEM_CS_sRGB, LISTITEM_CS_Linear };
        static const acfWChar* names[] = { L"sRGB", L"Linear" };
        static const acfUInt32 values[] = { 0, 1 };
        AddListBox(pFactory, pEffectDef, PARAMETER_Colorspace_GUID, LAYOUT_Colorspace_GUID,
                   L"Colorspace", L"Input colorspace for the model",
                   0, guids, names, values, 2);
    }

    // Time Awareness — analyzes the clip for temporal consistency (official CK feature)
    AddCheckbox(pFactory, pEffectDef, PARAMETER_TimeAware_GUID, LAYOUT_TimeAware_GUID,
                L"Time Awareness",
                L"Analyze the clip for temporal consistency (eliminates flicker)",
                kACFFalse);

    // Temporal Window — hann_chunk when Time Awareness is on (1–80 frames)
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_TemporalWindow_GUID, LAYOUT_TemporalWindow_GUID,
                    L"Temporal Window",
                    L"Frames per temporal window when Time Awareness is on (1-80)",
                    16, 1, 80);

    // Hint Quality (0–100%)
    AddUInt32Slider(pFactory, pEffectDef, PARAMETER_HintQuality_GUID, LAYOUT_HintQuality_GUID,
                    L"Hint Quality", L"How strongly to trust the Alpha Hint (0-100%)",
                    95, 0, 100);

    AddCheckbox(pFactory, pEffectDef, PARAMETER_CarryHint_GUID, LAYOUT_CarryHint_GUID,
                L"Carry Hint Alpha",
                L"Carry hint alpha across temporal windows",
                kACFFalse);
#endif

    // Do NOT claim realtime — MC may skip filling media and pass video-black buffers.
    // Effect still runs on scrub/render; Expert Render caches the result.
    acfBool bReal = kACFFalse;
    acfcheck(pEffectDef->DefineAttribute(AVXATTR_Effect_Realtime, ACFTypeID_Boolean, &bReal, sizeof(acfBool)));

    // IPC worker is not re-entrant across host render threads.
    acfBool bSingle = kACFTrue;
    acfcheck(pEffectDef->DefineAttribute(AVXATTR_Effect_RenderSingleThreaded, ACFTypeID_Boolean, &bSingle, sizeof(acfBool)));

    *ppComponentDefinition = pEffectDef.detach();

    END_ACF_METHOD
}

ACFAPI ACFGetClassFactory(
    IACFUnknown* pUnkHost,
    const acfCLSID& clsid,
    const acfIID& iid,
    void** ppOut)
{
    if (!pUnkHost || !ppOut)
        return ACF_E_INVALIDARG;

    ACFRESULT result = ACF_CLASS_E_CLASSNOTAVAILABLE;
    ACFCreateObjectProc createProc = 0;

    if (kEffectClass == clsid) {
        createProc = CCorridorKey_Effect::ACFCreate;
    }
#ifdef CK_PREVIEW_PROBE
    else if (CLSID_PreviewProbe_Dialog == clsid) {
        createProc = CorridorKeyPreviewProbe::ACFCreate;
    }
#endif

    if (createProc) {
        ACFPtr<IACFUnknown> pFactoryUnknown;
        result = CACFClassFactory::Create(createProc, IID_IACFUnknown, (void**)&pFactoryUnknown);
        if (ACFSUCCEEDED(result) && pFactoryUnknown)
            result = pFactoryUnknown->QueryInterface(iid, ppOut);
    }

    return result;
}

ACFAPI ACFCanUnloadNow(IACFUnknown* pUnkHost)
{
    if (!pUnkHost)
        return ACF_E_INVALIDARG;
    return CACFUnknown::GetActiveObjectCount() == 0 ? ACF_OK : ACF_E_FAIL;
}
