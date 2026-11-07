cmake_minimum_required(VERSION 3.22)
# Mac support targets Apple Silicon only; no Intel/Rosetta build is planned.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "ToneGrid Mac architecture")
endif()
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "ToneGrid minimum macOS version")
endif()
project(ToneGrid VERSION 1.1.44 LANGUAGES C CXX)
execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE TONEGRID_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
# Publication archive fallback; never obtain an unrelated parent checkout's revision.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git" OR NOT TONEGRID_SOURCE_REVISION)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/SOURCE-REVISION.txt" TONEGRID_ARCHIVE_REVISION LIMIT_COUNT 1)
    string(SUBSTRING "${TONEGRID_ARCHIVE_REVISION}" 0 12 TONEGRID_SOURCE_REVISION)
endif()
configure_file(Source/BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/ToneGridBuildInfo.h" @ONLY)
add_library(tonegrid_build_info INTERFACE)
target_include_directories(tonegrid_build_info INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
# Keep build output inside its own source checkout.
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" tonegrid_test_source)
file(REAL_PATH "${CMAKE_BINARY_DIR}" tonegrid_test_binary)
cmake_path(IS_PREFIX tonegrid_test_source "${tonegrid_test_binary}" NORMALIZE tonegrid_test_build_is_local)
if(NOT tonegrid_test_build_is_local OR tonegrid_test_source STREQUAL tonegrid_test_binary)
    message(FATAL_ERROR "ToneGrid requires a build subdirectory inside this checkout.")
endif()
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install-tonegrid" CACHE PATH "Test install destination" FORCE)
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR "ToneGrid targets Windows x64, macOS Apple Silicon and 64-bit Linux.")
endif()
if(APPLE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "The Mac target is macOS arm64 only.")
    endif()
    enable_language(OBJCXX)
endif()

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/JUCE-8.0.6" CACHE PATH "JUCE source directory")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE missing. Run build.ps1 or set JUCE_DIR.")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
if(WIN32)
    set(ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/asio/ASIOSDK" CACHE PATH "Steinberg ASIO SDK directory")
    if(NOT EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        message(FATAL_ERROR "ASIO SDK missing. Run build.ps1 or set ASIO_SDK_DIR.")
    endif()
endif()
if(WIN32 AND NOT JUCE_WEBVIEW2_PACKAGE_LOCATION)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/vendor/webview2" CACHE PATH "Parent of the pinned Microsoft.Web.WebView2 SDK" FORCE)
endif()
if(WIN32)
    set(tonegrid_browser_args NEEDS_WEB_BROWSER TRUE NEEDS_WEBVIEW2 TRUE)
    set(tonegrid_platform_definitions JUCE_WEB_BROWSER=1 JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1 JUCE_ASIO=1)
elseif(APPLE)
    set(tonegrid_browser_args NEEDS_WEB_BROWSER TRUE MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "ToneGrid needs audio input to process and record your guitar."
        PLIST_TO_MERGE "<plist><dict><key>LSMinimumSystemVersion</key><string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string></dict></plist>")
    set(tonegrid_platform_definitions JUCE_WEB_BROWSER=1 JUCE_ASIO=0)
else()
    # Native ALSA audio, curl HTTPS and JUCE's WebKitGTK browser backend when available.
    if(EXISTS "/usr/include/webkitgtk-4.0/webkit2/webkit2.h" OR EXISTS "/usr/include/webkitgtk-4.1/webkit2/webkit2.h")
        set(tonegrid_browser_args NEEDS_WEB_BROWSER TRUE)
        set(tonegrid_web_defs JUCE_WEB_BROWSER=1)
    else()
        set(tonegrid_browser_args NEEDS_WEB_BROWSER FALSE)
        set(tonegrid_web_defs JUCE_WEB_BROWSER=0)
    endif()
    if(EXISTS "/usr/include/alsa/asoundlib.h")
        set(tonegrid_platform_definitions JUCE_ASIO=0 ${tonegrid_web_defs})
    else()
        set(tonegrid_platform_definitions JUCE_ASIO=0 JUCE_ALSA=0 ${tonegrid_web_defs})
    endif()
endif()
juce_add_gui_app(ToneGrid VERSION "${PROJECT_VERSION}" PRODUCT_NAME "ToneGrid" COMPANY_NAME "Personal Audio Lab"
    BUNDLE_ID "com.personalaudiolab.tonexstateprobe" NEEDS_CURL TRUE ${tonegrid_browser_args})
set_target_properties(ToneGrid PROPERTIES OUTPUT_NAME "ToneGrid" RUNTIME_OUTPUT_DIRECTORY_RELEASE
    "${CMAKE_CURRENT_BINARY_DIR}/ToneGrid_artefacts/Release-${PROJECT_VERSION}")
if(APPLE)
    set(tonegrid_payload_dir "$<TARGET_BUNDLE_CONTENT_DIR:ToneGrid>/Resources")
else()
    set(tonegrid_payload_dir "$<TARGET_FILE_DIR:ToneGrid>")
endif()
include(cmake/ToneGridIcons.cmake)
juce_add_binary_data(ToneGridApplicationAssets NAMESPACE ToneGridApplicationAssets HEADER_NAME ToneGridApplicationAssets.h SOURCES
    Resources/AppIcons/Windows/ToneGrid-512.png Resources/Branding/Tone3000.png Resources/Branding/GuitarClub.png Resources/Artwork/record_headstock.svg Resources/Calibration/interface-profiles.json Resources/Jam/backing-tracks.json Resources/Learn/lessons.json Resources/Translations/en.json)
target_link_libraries(ToneGrid PRIVATE ToneGridApplicationAssets)
target_sources(ToneGrid PRIVATE Source/Shared/Media/VideoFullscreen.cpp Source/Shared/Localization/Language.cpp)
if(APPLE)
    target_sources(ToneGrid PRIVATE Source/Platform/VideoBrowserMac.mm Source/Platform/BrowserNavigationMac.mm)
    set_source_files_properties(Source/Shared/Media/VideoFullscreen.cpp PROPERTIES LANGUAGE OBJCXX)
endif()
if(WIN32)
    target_link_libraries(ToneGrid PRIVATE bcrypt)
endif()
set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/NeuralAmpModelerCore-main")
file(GLOB NAM_SOURCES CONFIGURE_DEPENDS "${NAM_ROOT}/NAM/*.cpp" "${NAM_ROOT}/NAM/wavenet/*.cpp")
# An object library retains NAM's static architecture registrars.
add_library(nam_core OBJECT ${NAM_SOURCES})
target_include_directories(nam_core PUBLIC "${NAM_ROOT}" "${NAM_ROOT}/Dependencies/nlohmann" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/eigen-3.4.0")
target_compile_features(nam_core PUBLIC cxx_std_20)
target_compile_definitions(nam_core PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
if(WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_compile_options(nam_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# Spring-only AELAPSE DSP, isolated from all plugin/UI/tape-delay code.
set(tonegrid_aelapse_include "${CMAKE_CURRENT_SOURCE_DIR}/vendor/aelapse-spring/include")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(tonegrid_aelapse_include "${CMAKE_CURRENT_BINARY_DIR}/aelapse-linux-shim/include")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/vendor/aelapse-spring/include" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/aelapse-linux-shim")
    file(READ "${tonegrid_aelapse_include}/dsp/simd/simd_x86_64.h" _aelapse_simd_src)
    string(REPLACE "static_assert(false, \"wrong duplicate\")" "static_assert(K == 0, \"wrong duplicate\")" _aelapse_simd_patched "${_aelapse_simd_src}")
    file(WRITE "${tonegrid_aelapse_include}/dsp/simd/simd_x86_64.h" "${_aelapse_simd_patched}")
endif()
add_library(tonegrid_spring STATIC Source/Effects/Reverb/SpringReverbProcessor.cpp
    vendor/aelapse-spring/processors/springs/Springs.cpp)
target_include_directories(tonegrid_spring PRIVATE "${tonegrid_aelapse_include}"
    vendor/aelapse-spring/gcem/include)
target_compile_features(tonegrid_spring PRIVATE cxx_std_20)
# No AVX dispatch, OpenMP or fast-math changes to existing ToneGrid DSP.
add_library(tonegrid_effects STATIC Source/Effects/Pitch/PitchEngine.cpp Source/Effects/Modulation/ModProcessor.cpp
    vendor/daisysp/Source/Effects/chorus.cpp vendor/daisysp/Source/Effects/tremolo.cpp
    vendor/daisysp/Source/Effects/phaser.cpp vendor/daisysp/Source/Effects/flanger.cpp
    vendor/daisysp/Source/Synthesis/oscillator.cpp)
target_include_directories(tonegrid_effects PRIVATE vendor/stft-pitch vendor/daisysp/Source vendor/daisysp/Source/Utility)
target_compile_features(tonegrid_effects PRIVATE cxx_std_20)
target_compile_definitions(tonegrid_effects PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 NOMINMAX)
target_link_libraries(tonegrid_effects PRIVATE juce::juce_dsp)
juce_add_binary_data(ToneGridBranding NAMESPACE ToneGridBranding HEADER_NAME ToneGridBranding.h SOURCES Resources/Branding/ToneGrid_logo_transparent.svg)
target_link_libraries(ToneGrid PRIVATE tonegrid_effects ToneGridBranding)
target_sources(ToneGrid PRIVATE Source/App/BoardMain.cpp Source/DeviceRateProcessor.cpp Source/SessionRecorder.cpp Source/Platform/PlatformFiles.cpp Source/Platform/Tone3000Platform.cpp Source/Effects/StereoSpread/StereoSpreadProcessor.cpp Source/Effects/Reverb/ReverbProcessor.cpp vendor/dattorro-verb/verb.c)
target_link_libraries(ToneGrid PRIVATE nam_core)
juce_add_binary_data(PedalArtwork SOURCES Resources/Artwork/comp-body.png Resources/ToneGrid/Embedded/skin01.png Resources/ToneGrid/Embedded/skin02.png Resources/ToneGrid/Embedded/skin03.png Resources/ToneGrid/Embedded/skin04.png Resources/ToneGrid/Embedded/skin05.png Resources/ToneGrid/Embedded/skin06.png Resources/ToneGrid/Embedded/skin07.png Resources/ToneGrid/Embedded/skin08.png Resources/ToneGrid/Embedded/skin09.png Resources/ToneGrid/Embedded/skin10.png Resources/ToneGrid/Embedded/skin11.png Resources/ToneGrid/Embedded/skin12.png Resources/ToneGrid/Embedded/skin13.png Resources/Artwork/amp-reference.png)
# Pilot assets are embedded under a separate namespace; existing twelve skins stay intact.
juce_add_binary_data(TransparentOdArtwork NAMESPACE TransparentOdAssets HEADER_NAME TransparentOdAssets.h SOURCES
    Resources/PedalAssets/TransparentOdFidelity/assets/pedals/transparent_od/face.png
    Resources/PedalAssets/TransparentOdFidelity/assets/pedals/transparent_od/transparent_od_layout.json
    Resources/PedalAssets/TransparentOdFidelity/assets/shared/knob_strip.png
    Resources/PedalAssets/TransparentOdPilot/assets/shared/led_off.png
    Resources/PedalAssets/TransparentOdPilot/assets/shared/led_on.png
    Resources/PedalAssets/TransparentOdPilot/assets/shared/footswitch_up.png
    Resources/PedalAssets/TransparentOdPilot/assets/shared/footswitch_down.png)
target_link_libraries(ToneGrid PRIVATE PedalArtwork)
target_link_libraries(ToneGrid PRIVATE TransparentOdArtwork)
juce_add_binary_data(PedalSkin137Artwork NAMESPACE PedalSkin137Assets HEADER_NAME PedalSkin137Assets.h SOURCES
    Resources/PedalAssets/V137/layout.json
    Resources/PedalAssets/V137/face01.png Resources/PedalAssets/V137/face02.png
    Resources/PedalAssets/V137/face03.png Resources/PedalAssets/V137/face04.png
    Resources/PedalAssets/V137/face05.png Resources/PedalAssets/V137/face06.png
    Resources/PedalAssets/V137/face07.png Resources/PedalAssets/V137/face08.png
    Resources/PedalAssets/V137/face09.png Resources/PedalAssets/V137/face10.png
    Resources/PedalAssets/V137/face11.png Resources/PedalAssets/V137/face12.png
    Resources/PedalAssets/V137/face13.png Resources/PedalAssets/V137/switch_high.png
    Resources/PedalAssets/V137/switch_low.png)
target_link_libraries(ToneGrid PRIVATE PedalSkin137Artwork)
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/PedalAssets/V137" "${tonegrid_payload_dir}/PedalSkins")
# Preserve original face filenames on disk; unique build aliases keep resource
# names unambiguous without changing or re-encoding any source PNG.
set(deployment_assets "${CMAKE_CURRENT_SOURCE_DIR}/Resources/PedalAssets/Deployment118/assets")
set(deployment_aliases "${CMAKE_CURRENT_BINARY_DIR}/pedal_asset_aliases")
file(MAKE_DIRECTORY "${deployment_aliases}")
set(deployment_sources)
foreach(pedal classic_fuzz treble_boost auto_wah studio_comp horse_od 808_od blues_od filter_dist orange_dist vintage_fuzz graphic_eq clean_boost)
    configure_file("${deployment_assets}/pedals/${pedal}/face.png" "${deployment_aliases}/p${pedal}_face.png" COPYONLY)
    configure_file("${deployment_assets}/pedals/${pedal}/layout.json" "${deployment_aliases}/p${pedal}_layout.json" COPYONLY)
    list(APPEND deployment_sources "${deployment_aliases}/p${pedal}_face.png" "${deployment_aliases}/p${pedal}_layout.json")
endforeach()
juce_add_binary_data(PedalDeploymentArtwork NAMESPACE PedalDeploymentAssets HEADER_NAME PedalDeploymentAssets.h SOURCES
    ${deployment_sources} "${deployment_assets}/shared/slider_cap.png" Resources/PedalAssets/Scene118/background.png)
target_link_libraries(ToneGrid PRIVATE PedalDeploymentArtwork)
target_link_libraries(ToneGrid PRIVATE tonegrid_spring)
target_compile_options(ToneGrid PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# Ship Microsoft's redistributable C++ runtime DLLs beside the portable executable.
# Keep the validated compiler/runtime configuration unchanged.
if(WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    foreach(runtime_file IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${runtime_file}" "$<TARGET_FILE_DIR:ToneGrid>")
    endforeach()
endif()
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Models" "${tonegrid_payload_dir}/Models")
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Licenses" "${tonegrid_payload_dir}/Licenses")
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Jam" "${tonegrid_payload_dir}/Jam")
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Learn" "${tonegrid_payload_dir}/Learn")
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Calibration" "${tonegrid_payload_dir}/Calibration")
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Translations" "${tonegrid_payload_dir}/Translations")
if(WIN32)
    target_include_directories(ToneGrid PRIVATE "${ASIO_SDK_DIR}/common")
endif()
target_compile_features(ToneGrid PRIVATE cxx_std_20)
target_compile_definitions(ToneGrid PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0
    JUCE_USE_CURL=$<BOOL:$<PLATFORM_ID:Linux>> ${tonegrid_platform_definitions}
    JUCE_APPLICATION_NAME_STRING="ToneGrid"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    TONEGRID_VERSION_NUMERIC="${PROJECT_VERSION}")
target_link_libraries(ToneGrid PRIVATE juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# No RigHost, audio device or user bank is constructed by the offline runner.
include(cmake/ToneGridLame.cmake)
include(CTest)
if(BUILD_TESTING)
    if(APPLE)
        add_test(NAME ToneGrid.Tone3000NativeUrl COMMAND ToneGrid --tone3000-native-url-test)
        set_tests_properties(ToneGrid.Tone3000NativeUrl PROPERTIES LABELS "offline;host;browser" TIMEOUT 30)
    endif()
    juce_add_console_app(ToneGridOfflineTests NEEDS_CURL TRUE PRODUCT_NAME "ToneGrid Offline Tests")
    target_sources(ToneGridOfflineTests PRIVATE Tests/Reference/Reverb115Processor.cpp Tests/OfflineTests.cpp Tests/Language144Tests.cpp Source/Shared/Localization/Language.cpp Source/DeviceRateProcessor.cpp Tests/RealtimeAllocationProbe.cpp Source/SessionRecorder.cpp Source/Platform/PlatformFiles.cpp Source/Platform/Tone3000Platform.cpp Source/Effects/StereoSpread/StereoSpreadProcessor.cpp Source/Effects/Reverb/ReverbProcessor.cpp vendor/dattorro-verb/verb.c)
    target_include_directories(ToneGridOfflineTests PRIVATE Source)
    if(WIN32)
        target_include_directories(ToneGridOfflineTests PRIVATE "${ASIO_SDK_DIR}/common")
    endif()
    target_compile_definitions(ToneGridOfflineTests PRIVATE
        TONEGRID_TEST_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Reference"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    TONEGRID_VERSION_NUMERIC="${PROJECT_VERSION}"
        JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=$<BOOL:$<PLATFORM_ID:Linux>> ${tonegrid_platform_definitions})
    target_link_libraries(ToneGridOfflineTests PRIVATE tonegrid_spring tonegrid_effects ToneGridBranding ToneGridApplicationAssets)
    if(WIN32)
        target_link_libraries(ToneGridOfflineTests PRIVATE bcrypt)
    endif()
    target_compile_options(ToneGridOfflineTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(ToneGridOfflineTests PRIVATE nam_core PedalArtwork TransparentOdArtwork PedalDeploymentArtwork PedalSkin137Artwork
        juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    set(tonegrid_test_models "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Models")
    add_test(NAME ToneGrid.BassMode COMMAND ToneGridOfflineTests --bass-mode-test "${tonegrid_test_models}")
    set_tests_properties(ToneGrid.BassMode PROPERTIES LABELS "offline;bass;dsp;ui" TIMEOUT 120)
    add_test(NAME ToneGrid.Regression140 COMMAND ToneGridOfflineTests --regression140-test)
    set_tests_properties(ToneGrid.Regression140 PROPERTIES LABELS "offline;ui;keyboard;hover" RUN_SERIAL TRUE TIMEOUT 60)
    add_test(NAME ToneGrid.LibraryEscape COMMAND ToneGridOfflineTests --library-escape-test)
    set_tests_properties(ToneGrid.LibraryEscape PROPERTIES LABELS "offline;ui;keyboard;library" TIMEOUT 30)
    add_test(NAME ToneGrid.Regression139 COMMAND ToneGridOfflineTests --regression139-test)
    set_tests_properties(ToneGrid.Regression139 PROPERTIES LABELS "offline;ui;keyboard" TIMEOUT 30)
    add_test(NAME ToneGrid.Refinements138 COMMAND ToneGridOfflineTests --refinements138-test "${tonegrid_test_models}")
    set_tests_properties(ToneGrid.Refinements138 PROPERTIES LABELS "offline;ui;presets" TIMEOUT 120)
    add_test(NAME ToneGrid.UiMotion138 COMMAND ToneGridOfflineTests --motion138-test)
    set_tests_properties(ToneGrid.UiMotion138 PROPERTIES LABELS "offline;ui" TIMEOUT 60)
    add_test(NAME ToneGrid.PedalSkin137 COMMAND ToneGridOfflineTests --pedal137-test "${tonegrid_test_models}" "${CMAKE_CURRENT_BINARY_DIR}/PedalSkin137Tests")
    set_tests_properties(ToneGrid.PedalSkin137 PROPERTIES LABELS "offline;ui;pedals;midi" TIMEOUT 120)
    add_test(NAME ToneGrid.InputCompensation136 COMMAND ToneGridOfflineTests --input-compensation136-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryAmps")
    set_tests_properties(ToneGrid.InputCompensation136 PROPERTIES LABELS "offline;nam;input-compensation" TIMEOUT 120)
    add_test(NAME ToneGrid.Offline COMMAND ToneGridOfflineTests
        "${tonegrid_test_models}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures/compatibility.xml")
    set_tests_properties(ToneGrid.Offline PROPERTIES LABELS "offline" TIMEOUT 180)
    add_test(NAME ToneGrid.RigStorage COMMAND ToneGridOfflineTests --rig-storage-test "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures/compatibility.xml")
    set_tests_properties(ToneGrid.RigStorage PROPERTIES LABELS "offline;presets;storage" TIMEOUT 60)
    add_test(NAME ToneGrid.PrivateBeta125Host COMMAND ToneGrid --private-beta125-ui-test)
    add_test(NAME ToneGrid.Starter133LegacyHost COMMAND ToneGrid --starter133-legacy-ui-test)
    add_test(NAME ToneGrid.Starter133ExistingHost COMMAND ToneGrid --starter133-existing-ui-test)
    set_tests_properties(ToneGrid.Starter133LegacyHost ToneGrid.Starter133ExistingHost PROPERTIES LABELS "offline;beta;host;startup" TIMEOUT 180)
    set_tests_properties(ToneGrid.PrivateBeta125Host PROPERTIES LABELS "offline;beta;host" TIMEOUT 180)
    add_test(NAME ToneGrid.BetaWelcome125Host COMMAND ToneGrid --beta-welcome125-ui-test)
    set_tests_properties(ToneGrid.BetaWelcome125Host PROPERTIES LABELS "offline;beta;host" TIMEOUT 60)
    add_test(NAME ToneGrid.Normalization125Host COMMAND ToneGrid --normalization125-ui-test)
    set_tests_properties(ToneGrid.Normalization125Host PROPERTIES LABELS "offline;normalization;host" TIMEOUT 60)
    add_test(NAME ToneGrid.Normalization125 COMMAND ToneGridOfflineTests --normalization125-test)
    set_tests_properties(ToneGrid.Normalization125 PROPERTIES LABELS "offline;normalization" TIMEOUT 30)
    add_test(NAME ToneGrid.CalibrationHost COMMAND ToneGrid --calibration118-ui-test)
    set_tests_properties(ToneGrid.CalibrationHost PROPERTIES LABELS "offline;host;calibration" TIMEOUT 60)
    add_test(NAME ToneGrid.Calibration COMMAND ToneGridOfflineTests --calibration118-test)
    set_tests_properties(ToneGrid.Calibration PROPERTIES LABELS "offline;calibration" TIMEOUT 30)
    add_test(NAME ToneGrid.Portability COMMAND ToneGridOfflineTests --portability-test)
    set_tests_properties(ToneGrid.Portability PROPERTIES LABELS "offline;portability" TIMEOUT 30)
    add_test(NAME ToneGrid.Distribution COMMAND ToneGridOfflineTests --distribution116-test
        "${tonegrid_payload_dir}/FactoryAmps")
    set_tests_properties(ToneGrid.Distribution PROPERTIES LABELS "offline;distribution" TIMEOUT 180)
    add_test(NAME ToneGrid.Recording COMMAND ToneGridOfflineTests --recording-test
        "${tonegrid_encoder_destination}")
    set_tests_properties(ToneGrid.Recording PROPERTIES LABELS "offline;recording" TIMEOUT 90)
endif()

# JUCE 8.0.6's RC generator omits its version-info input dependency.
# Keep Windows version resources current on incremental product-version builds.
foreach(tonegrid_target IN ITEMS ToneGrid ToneGridOfflineTests)
    if(TARGET ${tonegrid_target})
        if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
            # Retain the working Mac 1.1.39 slider snapping policy. Implicit
            # FMA can turn a legal zero/saved value into a tiny residual.
            # NAM Core and standalone DSP targets keep their existing flags.
            target_compile_options(${tonegrid_target} PRIVATE -ffp-contract=off)
        endif()
        if(WIN32)
            target_link_options(${tonegrid_target} PRIVATE "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/cmake/WindowsLongPaths.manifest")
        endif()
        if(NOT WIN32)
            continue()
        endif()
        get_target_property(tonegrid_code_dir ${tonegrid_target} JUCE_GENERATED_SOURCES_DIRECTORY)
        get_target_property(tonegrid_info_file ${tonegrid_target} JUCE_INFO_FILE)
        add_custom_command(OUTPUT "${tonegrid_code_dir}/${tonegrid_target}_resources.rc"
            APPEND DEPENDS "${tonegrid_info_file}")
    endif()
endforeach()


add_custom_command(TARGET ToneGrid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/StarterPresets" "${tonegrid_payload_dir}/StarterPresets")
# ToneBank content remains a separate, read-only distribution payload.
add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ToneBank" "${tonegrid_payload_dir}/ToneBank")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryAmps/manifest.json")
    add_custom_command(TARGET ToneGrid POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${tonegrid_payload_dir}/FactoryAmps/05 - High Gain/Silver_Jubilee_1987_INTERNAL USE ONLY"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryAmps" "${tonegrid_payload_dir}/FactoryAmps")
endif()
add_custom_command(TARGET ToneGrid POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt" "${tonegrid_payload_dir}/LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE-NOTICE.txt" "${tonegrid_payload_dir}/LICENSE-NOTICE.txt")
# Runtime content may change without recompiling C++. Native build scripts build
# ALL, then sign on macOS: always copy the verified resources before that signature.
add_custom_target(ToneGridRuntimeContent ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/PedalAssets/V137" "${tonegrid_payload_dir}/PedalSkins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Models" "${tonegrid_payload_dir}/Models"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryAmps" "${tonegrid_payload_dir}/FactoryAmps"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Licenses" "${tonegrid_payload_dir}/Licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/ToneBank" "${tonegrid_payload_dir}/ToneBank"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/StarterPresets" "${tonegrid_payload_dir}/StarterPresets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Jam" "${tonegrid_payload_dir}/Jam"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Learn" "${tonegrid_payload_dir}/Learn"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Calibration" "${tonegrid_payload_dir}/Calibration"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Translations" "${tonegrid_payload_dir}/Translations"
    DEPENDS ToneGrid
    COMMENT "Synchronizing versioned ToneGrid runtime content"
    VERBATIM)
if(TARGET ToneGridOfflineTests)
    target_compile_definitions(ToneGridOfflineTests PRIVATE TONEGRID_BANK_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Resources/ToneBank/tonebank.json")
    add_test(NAME ToneGrid.ToneBank133 COMMAND ToneGridOfflineTests --tonebank133-test "${tonegrid_test_models}")
    set_tests_properties(ToneGrid.ToneBank133 PROPERTIES LABELS "offline;presets;compatibility" TIMEOUT 60)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.Release120 COMMAND ToneGridOfflineTests --release120-test "${tonegrid_payload_dir}" "${CMAKE_CURRENT_BINARY_DIR}/Release120Evidence")
    set_tests_properties(ToneGrid.Release120 PROPERTIES LABELS "offline;distribution;library" TIMEOUT 300)
    add_test(NAME ToneGrid.Selectors120 COMMAND ToneGridOfflineTests --capture-selector120-test "${tonegrid_test_models}" "${CMAKE_CURRENT_BINARY_DIR}/Selector120Evidence")
    set_tests_properties(ToneGrid.Selectors120 PROPERTIES LABELS "offline;ui" TIMEOUT 60)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.AmpEq121 COMMAND ToneGridOfflineTests --amp-eq121-test)
    set_tests_properties(ToneGrid.AmpEq121 PROPERTIES LABELS "offline;eq" TIMEOUT 60)
    add_test(NAME ToneGrid.AmpEq121Host COMMAND ToneGrid --amp-eq121-ui-test)
    set_tests_properties(ToneGrid.AmpEq121Host PROPERTIES LABELS "offline;host;eq" TIMEOUT 60)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.Release122 COMMAND ToneGridOfflineTests --release122-test "${tonegrid_test_models}")
    set_tests_properties(ToneGrid.Release122 PROPERTIES LABELS "offline;library;eq;looper;sharing" TIMEOUT 180)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.Release122Host COMMAND ToneGrid --release122-ui-test)
    set_tests_properties(ToneGrid.Release122Host PROPERTIES LABELS "offline;host;library;eq;sharing" TIMEOUT 90)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.Ui123Host COMMAND ToneGrid --ui123-test)
    add_test(NAME ToneGrid.Window125Host COMMAND ToneGrid --window125-test)
    add_test(NAME ToneGrid.Popups126 COMMAND ToneGridOfflineTests --popup126-test "${CMAKE_CURRENT_BINARY_DIR}/Popup126Evidence")
    add_test(NAME ToneGrid.Diagnostic126 COMMAND ToneGridOfflineTests --diagnostic126-test)
    set_tests_properties(ToneGrid.Diagnostic126 PROPERTIES LABELS "offline;ui;diagnostic" TIMEOUT 30)
    add_test(NAME ToneGrid.AudioRecovery126Host COMMAND ToneGrid --audio-recovery126-ui-test)
    set_tests_properties(ToneGrid.AudioRecovery126Host PROPERTIES LABELS "offline;host;audio" TIMEOUT 90)
    set_tests_properties(ToneGrid.Popups126 PROPERTIES LABELS "offline;ui" TIMEOUT 90)
    set_tests_properties(ToneGrid.Window125Host PROPERTIES LABELS "offline;host;ui" TIMEOUT 90)
    set_tests_properties(ToneGrid.Ui123Host PROPERTIES LABELS "offline;host;ui" TIMEOUT 90)
    add_test(NAME ToneGrid.Library124 COMMAND ToneGridOfflineTests --library124-test)
    set_tests_properties(ToneGrid.Library124 PROPERTIES LABELS "offline;library" TIMEOUT 90)
    add_test(NAME ToneGrid.Library124Host COMMAND ToneGrid --library124-ui-test)
    set_tests_properties(ToneGrid.Library124Host PROPERTIES LABELS "offline;host;library;ui" TIMEOUT 90)
    add_test(NAME ToneGrid.NamEq123 COMMAND ToneGridOfflineTests --nam-eq123-test)
    set_tests_properties(ToneGrid.NamEq123 PROPERTIES LABELS "offline;eq" TIMEOUT 90)
    add_test(NAME ToneGrid.NamEq123Host COMMAND ToneGrid --nam-eq123-ui-test)
    set_tests_properties(ToneGrid.NamEq123Host PROPERTIES LABELS "offline;host;eq;ui" TIMEOUT 90)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.DeviceRate COMMAND ToneGridOfflineTests --device-rate-test)
    set_tests_properties(ToneGrid.DeviceRate PROPERTIES LABELS "offline;audio;resampling" TIMEOUT 120)
endif()

if(BUILD_TESTING)
    add_test(NAME ToneGrid.Release130 COMMAND ToneGridOfflineTests --release130-test "${CMAKE_CURRENT_BINARY_DIR}/Release130Evidence")
    set_tests_properties(ToneGrid.Release130 PROPERTIES LABELS "offline;ui;eq;jam" TIMEOUT 120)
    add_test(NAME ToneGrid.Release130Host COMMAND ToneGrid --release130-ui-test)
    set_tests_properties(ToneGrid.Release130Host PROPERTIES LABELS "offline;host;ui;eq;jam" TIMEOUT 90)
endif()

if(BUILD_TESTING)
    add_test(NAME ToneGrid.Learn132 COMMAND ToneGridOfflineTests --learn132-test "${CMAKE_CURRENT_BINARY_DIR}/Learn132Evidence")
    target_sources(ToneGridOfflineTests PRIVATE Source/Shared/Media/VideoFullscreen.cpp)
    set_tests_properties(ToneGrid.Learn132 PROPERTIES LABELS "offline;ui;learn" TIMEOUT 120)
    add_test(NAME ToneGrid.Learn132Host COMMAND ToneGrid --learn132-ui-test)
    set_tests_properties(ToneGrid.Learn132Host PROPERTIES LABELS "offline;host;ui;learn" TIMEOUT 90)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.Release134 COMMAND ToneGridOfflineTests --release134-test "${tonegrid_payload_dir}")
    add_test(NAME ToneGrid.Release134Host COMMAND ToneGrid --release134-ui-test)
    set_tests_properties(ToneGrid.Release134 ToneGrid.Release134Host PROPERTIES LABELS "offline;calibration;metadata;normalization" TIMEOUT 120)
    add_test(NAME ToneGrid.Release135 COMMAND ToneGridOfflineTests --release135-test)
    add_test(NAME ToneGrid.Release135Host COMMAND ToneGrid --release135-ui-test)
    set_tests_properties(ToneGrid.Release135 ToneGrid.Release135Host PROPERTIES LABELS "offline;vst;portability" TIMEOUT 120)
    add_test(NAME ToneGrid.Startup135Invalid COMMAND ToneGrid --startup135-invalid)
    add_test(NAME ToneGrid.Startup135MissingNam COMMAND ToneGrid --startup135-missing-nam)
    add_test(NAME ToneGrid.Startup135MissingVst COMMAND ToneGrid --startup135-missing-vst)
    add_test(NAME ToneGrid.Startup135Interrupted COMMAND ToneGrid --startup135-interrupted)
    set_tests_properties(ToneGrid.Startup135Interrupted PROPERTIES LABELS "offline;startup;portability" TIMEOUT 120)
    set_tests_properties(ToneGrid.Startup135Invalid ToneGrid.Startup135MissingNam ToneGrid.Startup135MissingVst PROPERTIES LABELS "offline;startup;portability" TIMEOUT 120)
endif()

if(TARGET ToneGridOfflineTests)
    add_test(NAME ToneGrid.Isolation COMMAND ToneGridOfflineTests --test-isolation)
    set_tests_properties(ToneGrid.Isolation PROPERTIES LABELS "offline;isolation;presets" TIMEOUT 60)
endif()

add_custom_command(TARGET ToneGrid POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryCabinetIR" "${tonegrid_payload_dir}/FactoryCabinetIR")
add_custom_target(ToneGridCabinetContent ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Resources/FactoryCabinetIR" "${tonegrid_payload_dir}/FactoryCabinetIR"
    DEPENDS ToneGrid VERBATIM)

if(TARGET ToneGridOfflineTests)
 add_test(NAME ToneGrid.Cabinet COMMAND ToneGridOfflineTests --cabinet-ir-test)
 set_tests_properties(ToneGrid.Cabinet PROPERTIES LABELS "offline;cabinet;presets" TIMEOUT 60)
endif()

if(TARGET ToneGridOfflineTests)
 add_test(NAME ToneGrid.PopupOnboarding COMMAND ToneGridOfflineTests --popup-onboarding-test)
 set_tests_properties(ToneGrid.PopupOnboarding PROPERTIES LABELS "offline;ui;calibration" TIMEOUT 30 RUN_SERIAL TRUE)
endif()

if(TARGET ToneGridOfflineTests)
 add_test(NAME ToneGrid.Optimized140 COMMAND ToneGridOfflineTests --optimized140-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "${CMAKE_CURRENT_BINARY_DIR}/Optimized140Evidence")
 set_tests_properties(ToneGrid.Optimized140 PROPERTIES LABELS "offline;ui;presets;isolation" RUN_SERIAL TRUE TIMEOUT 120)
 add_test(NAME ToneGrid.NamControls140 COMMAND ToneGridOfflineTests --nam-controls-test)
 set_tests_properties(ToneGrid.NamControls140 PROPERTIES LABELS "offline;audio;presets" TIMEOUT 60)
endif()


# Main-tab source modules. Static/INTERFACE targets only; no module DLLs.
add_library(tonegrid_core STATIC Source/Core/ProcessingGraph.cpp)
target_include_directories(tonegrid_core PRIVATE Source
    "${JUCE_DIR}/modules" "${NAM_ROOT}" "${NAM_ROOT}/Dependencies/nlohmann" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/eigen-3.4.0")
target_compile_features(tonegrid_core PUBLIC cxx_std_20)
target_compile_definitions(tonegrid_core PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_USE_MP3AUDIOFORMAT=1 NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(tonegrid_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(tonegrid_core INTERFACE tonegrid_effects tonegrid_spring)
target_link_libraries(tonegrid_core PUBLIC tonegrid_build_info)
target_include_directories(tonegrid_effects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(tonegrid_spring PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
add_library(tonegrid_record STATIC Source/Modules/Record/RecordModule.cpp Source/Modules/Record/RecordController.cpp)
target_include_directories(tonegrid_record PRIVATE Source "${JUCE_DIR}/modules" "${NAM_ROOT}/Dependencies/nlohmann")
target_compile_features(tonegrid_record PUBLIC cxx_std_20)
target_compile_definitions(tonegrid_record PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 NOMINMAX WIN32_LEAN_AND_MEAN ${tonegrid_platform_definitions})
target_compile_options(tonegrid_record PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
target_link_libraries(tonegrid_record PUBLIC tonegrid_core PRIVATE PedalArtwork ToneGridBranding ToneGridApplicationAssets)
foreach(module Play Jam Learn)
    string(TOLOWER "${module}" tab)
    add_library(tonegrid_${tab} STATIC "Source/Modules/${module}/${module}Module.cpp")
    target_include_directories(tonegrid_${tab} PRIVATE Source "${JUCE_DIR}/modules"
        "${NAM_ROOT}" "${NAM_ROOT}/Dependencies/nlohmann" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/eigen-3.4.0")
    target_compile_features(tonegrid_${tab} PUBLIC cxx_std_20)
    target_compile_definitions(tonegrid_${tab} PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=$<BOOL:$<PLATFORM_ID:Linux>>
        JUCE_PLUGINHOST_VST3=1 JUCE_USE_MP3AUDIOFORMAT=1 NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST
        NOMINMAX WIN32_LEAN_AND_MEAN ${tonegrid_platform_definitions})
    target_compile_options(tonegrid_${tab} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    if(APPLE)
        # Match the Mac host policy for inline application DSP in feature headers.
        target_compile_options(tonegrid_${tab} PRIVATE -ffp-contract=off)
    endif()
    target_link_libraries(tonegrid_${tab} PUBLIC tonegrid_core PRIVATE PedalArtwork
        TransparentOdArtwork PedalDeploymentArtwork PedalSkin137Artwork ToneGridBranding ToneGridApplicationAssets)
endforeach()
target_include_directories(ToneGrid PRIVATE Source)
target_link_libraries(ToneGrid PRIVATE tonegrid_record tonegrid_play tonegrid_jam tonegrid_learn)
target_compile_definitions(ToneGrid PRIVATE JUCE_USE_MP3AUDIOFORMAT=1)
if(TARGET ToneGridOfflineTests)
    target_link_libraries(ToneGridOfflineTests PRIVATE tonegrid_core)
    add_test(NAME ToneGrid.Audit142 COMMAND ToneGridOfflineTests --audit142-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
    add_test(NAME ToneGrid.LibraryFeedback142 COMMAND ToneGridOfflineTests --library-feedback142-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
    add_test(NAME ToneGrid.FactoryRefinements142 COMMAND ToneGridOfflineTests --factory-refinements142-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
    add_test(NAME ToneGrid.Startup143ExistingHost COMMAND ToneGrid --startup143-existing-ui-test)
    set_tests_properties(ToneGrid.Startup143ExistingHost PROPERTIES LABELS "native;startup;ui;audio" TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME ToneGrid.Startup143Host COMMAND ToneGrid --startup143-ui-test)
    set_tests_properties(ToneGrid.Startup143Host PROPERTIES LABELS "native;startup;ui;audio" TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME ToneGrid.Release143 COMMAND ToneGridOfflineTests --release143-test "${CMAKE_CURRENT_BINARY_DIR}/Release143Evidence")
    add_test(NAME ToneGrid.Language144 COMMAND ToneGridOfflineTests --language144-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "${CMAKE_CURRENT_BINARY_DIR}/Language144Evidence")
    set_tests_properties(ToneGrid.Language144 PROPERTIES LABELS "offline;ui;localization" TIMEOUT 90 RUN_SERIAL TRUE)
    set_tests_properties(ToneGrid.Release143 PROPERTIES LABELS "offline;startup;cabinet;ui" TIMEOUT 60)
    add_test(NAME ToneGrid.FactoryRefinements142Host COMMAND ToneGrid --factory-refinements142-ui-test)
    set_tests_properties(ToneGrid.FactoryRefinements142Host PROPERTIES LABELS "native;presets;ui" TIMEOUT 240 RUN_SERIAL TRUE)
    set_tests_properties(ToneGrid.FactoryRefinements142 PROPERTIES LABELS "offline;library;presets;migration" TIMEOUT 90 RUN_SERIAL TRUE)
    set_tests_properties(ToneGrid.LibraryFeedback142 PROPERTIES LABELS "offline;library;content;ui" TIMEOUT 90 RUN_SERIAL TRUE)
    set_tests_properties(ToneGrid.Audit142 PROPERTIES LABELS "offline;architecture;content;presets" TIMEOUT 90 RUN_SERIAL TRUE)
    target_compile_definitions(ToneGridOfflineTests PRIVATE JUCE_USE_MP3AUDIOFORMAT=1)
    add_test(NAME ToneGrid.Record141 COMMAND ToneGridOfflineTests --record141-test "${CMAKE_CURRENT_SOURCE_DIR}/Resources" "$<TARGET_FILE:ToneGridLame>" "${CMAKE_CURRENT_BINARY_DIR}/Record141Evidence")
    set_tests_properties(ToneGrid.Record141 PROPERTIES LABELS "offline;record;audio;export;presets" TIMEOUT 240 RUN_SERIAL TRUE)
endif()

if(BUILD_TESTING)
    add_test(NAME ToneGrid.Record141Host COMMAND ToneGrid --record141-ui-test)
    set_tests_properties(ToneGrid.Record141Host PROPERTIES LABELS "offline;host;record;audio;export" TIMEOUT 240 RUN_SERIAL TRUE)
endif()

# Display version and Windows numeric resources come from PROJECT_VERSION.
if(WIN32)
    target_compile_definitions(ToneGrid PRIVATE
        JUCE_USER_DEFINED_RC_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Resources/VersionInfo142.rc")
endif()

find_program(TONEGRID_NODE_EXECUTABLE NAMES node)
if(BUILD_TESTING AND TONEGRID_NODE_EXECUTABLE)
    add_test(NAME ToneGrid.RecordVideoBridge COMMAND "${TONEGRID_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Tests/RecordVideoBridgeTests.cjs")
    set_tests_properties(ToneGrid.RecordVideoBridge PROPERTIES LABELS "offline;record;video" TIMEOUT 30)
endif()
