if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
  message(FATAL_ERROR "Configure with -DCMAKE_OSX_ARCHITECTURES=arm64.")
endif()

set(CORRIDORKEY_AVX_PROBE "composite" CACHE STRING "Timeline compositor: alpha or composite")
set_property(CACHE CORRIDORKEY_AVX_PROBE PROPERTY STRINGS alpha composite)
option(CORRIDORKEY_AVX_SYNTHETIC "Render the synthetic three-band matte instead of running inference" OFF)
option(CORRIDORKEY_AVX_PREVIEW_PROBE "Build the separate AVX2 dialog input test" OFF)
if(NOT CORRIDORKEY_AVX_PROBE MATCHES "^(alpha|composite)$")
  message(FATAL_ERROR "CORRIDORKEY_AVX_PROBE must be alpha or composite.")
endif()
if(CORRIDORKEY_AVX_PREVIEW_PROBE AND NOT CORRIDORKEY_AVX_PROBE STREQUAL "composite")
  message(FATAL_ERROR "The dialog input test requires the two-input compositor.")
endif()

set(ACF_PLATFORM_OPTIONS "")
file(READ "${AVX_INC}/acfbasetypes.h" ACF_BASETYPES)
# The 2016 evaluation toolkit predates ARM64. Its existing macOS LP64 types
# have the required widths. Generate the adjusted private header at build time.
if(NOT ACF_BASETYPES MATCHES "__arm64__|__aarch64__")
  set(INTEL_MAC_TEST "defined(__x86_64__) && defined(__APPLE__)")
  if(NOT ACF_BASETYPES MATCHES "defined\\(__x86_64__\\) && defined\\(__APPLE__\\)")
    message(FATAL_ERROR "Cannot identify the LP64 macOS types in this AVX toolkit.")
  endif()
  string(REPLACE "${INTEL_MAC_TEST}"
    "(defined(__x86_64__) || defined(__aarch64__)) && defined(__APPLE__)"
    ACF_BASETYPES "${ACF_BASETYPES}")
  set(ACF_ARM_HEADER "${CMAKE_CURRENT_BINARY_DIR}/acfbasetypes-arm64.h")
  file(WRITE "${ACF_ARM_HEADER}" "${ACF_BASETYPES}")
  set(ACF_PLATFORM_OPTIONS -include "${ACF_ARM_HEADER}")
endif()

add_library(CorridorKey MODULE
  src/CorridorKeyPlugin.cpp
  src/CorridorKeyEffect_mac.cpp
  "${AVX_UTILS}/CACFClassFactory.cpp"
  "${AVX_UTILS}/CACFUnknown.cpp"
)
target_include_directories(CorridorKey PRIVATE "${AVX_INC}" "${AVX_UTILS}")
target_compile_definitions(CorridorKey PRIVATE ACFPLUGIN_EXPORTS)
target_compile_options(CorridorKey PRIVATE ${ACF_PLATFORM_OPTIONS})
if(CORRIDORKEY_AVX_PROBE STREQUAL "composite")
  target_compile_definitions(CorridorKey PRIVATE CK_EXPLICIT_COMPOSITE)
endif()
if(CORRIDORKEY_AVX_PREVIEW_PROBE)
  enable_language(OBJCXX)
  set_target_properties(CorridorKey PROPERTIES
    OUTPUT_NAME CorridorKeyPreviewProbe
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED ON)
  target_compile_definitions(CorridorKey PRIVATE CK_SYNTHETIC_MATTE CK_PREVIEW_PROBE)
  target_sources(CorridorKey PRIVATE src/PreviewProbe_mac.mm)
  set_source_files_properties(src/PreviewProbe_mac.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  target_link_libraries(CorridorKey PRIVATE "-framework AppKit")
  set(CORRIDORKEY_BUNDLE_ID "org.corridorkey.community.avx.preview-probe")
elseif(CORRIDORKEY_AVX_SYNTHETIC)
  target_compile_definitions(CorridorKey PRIVATE CK_SYNTHETIC_MATTE)
else()
  set(WORKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/macos_worker.py")
  target_sources(CorridorKey PRIVATE src/MacFrameWorker.cpp "${WORKER_SCRIPT}")
  set_source_files_properties("${WORKER_SCRIPT}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_property(TARGET CorridorKey APPEND PROPERTY LINK_DEPENDS "${WORKER_SCRIPT}")
  target_link_libraries(CorridorKey PRIVATE "-framework CoreFoundation")
  target_link_options(CorridorKey PRIVATE "LINKER:-exported_symbol,_ACFShutdown")
endif()
if(NOT CORRIDORKEY_AVX_PREVIEW_PROBE)
  set(CORRIDORKEY_BUNDLE_ID "org.corridorkey.community.avx")
endif()
target_link_options(CorridorKey PRIVATE
  "LINKER:-exported_symbol,_ACFRegisterPlugin"
  "LINKER:-exported_symbol,_ACFRegisterComponent"
  "LINKER:-exported_symbol,_ACFGetClassFactory"
  "LINKER:-exported_symbol,_ACFCanUnloadNow"
)

set_target_properties(CorridorKey PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION avx
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacAVXInfo.plist"
)
add_custom_command(TARGET CorridorKey POST_BUILD
  COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:CorridorKey>"
  VERBATIM
)
