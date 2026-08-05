# Read semantic version (do not bump at configure time — builds bump via PRE_BUILD).
set(AUFX_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${AUFX_VERSION_FILE}" AUFX_VERSION_LINES)
list(GET AUFX_VERSION_LINES 0 AUFX_SEMVER)
string(STRIP "${AUFX_SEMVER}" AUFX_SEMVER)

set(AUFX_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${AUFX_GEN_DIR}")

# Seed generated sources so the first configure can resolve the target sources.
if(NOT EXISTS "${AUFX_GEN_DIR}/AppVersion.h")
    set(AUFX_BUILD 0)
    set(AUFX_VERSION_FULL "${AUFX_SEMVER}+0")
    string(TIMESTAMP AUFX_BUILD_YEAR "%Y")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppVersion.h.in"
        "${AUFX_GEN_DIR}/AppVersion.h"
        @ONLY)
    file(WRITE "${AUFX_GEN_DIR}/AppVersion.cpp"
"#include \"AppVersion.h\"
namespace aufx_version {
    const char* appName() { return AUFX_APP_NAME; }
    const char* versionString() { return AUFX_VERSION_STRING; }
    const char* buildYear() { return AUFX_BUILD_YEAR; }
    int buildNumber() { return AUFX_VERSION_BUILD; }
}
")
endif()

juce_add_gui_app(plugin_host_app
    PRODUCT_NAME "AU Effects Explorer"
    VERSION "${AUFX_SEMVER}"
    BUNDLE_ID "com.aufxtest.AUEffectsExplorer"
    COMPANY_NAME "Mike Jennings"
    COMPANY_COPYRIGHT "Copyright (c) Mike Jennings"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/app_icon.png"
    PLUGINHOST_AU TRUE
    PLUGINHOST_VST3 TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "AU Effects Explorer needs microphone access when a plugin (for example a sampler) records from an audio input device.")

set_target_properties(plugin_host_app PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.aufxtest.AUEffectsExplorer"
    MACOSX_BUNDLE_BUNDLE_NAME "AU Effects Explorer"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.aufxtest.AUEffectsExplorer"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_LSApplicationCategoryType "public.app-category.music"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_NSHighResolutionCapable YES)

juce_add_binary_data(plugin_host_app_assets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Resources/app_icon.png)

set(QV_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../quadraverse_app/Source")

target_include_directories(plugin_host_app
    PRIVATE
        Source
        ${AUFX_GEN_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../plugin_renderer/Source
        ${QV_SRC}
        ${QV_SRC}/domain
        ${QV_SRC}/formats)

target_sources(plugin_host_app
    PRIVATE
        Source/Main.cpp
        Source/HostConfig.cpp
        Source/HostPreferences.cpp
        Source/HostLog.cpp
        Source/HostLookAndFeel.cpp
        Source/MainWindow.cpp
        Source/MainContent.cpp
        Source/HostChromeControls.cpp
        Source/PresetHardwareState.cpp
        Source/SourceClipLibrary.cpp
        Source/CapturePipeline.cpp
        Source/NoiseFloorCalibration.cpp
        Source/LevelSweepCalibration.cpp
        Source/LevelMetrics.cpp
        Source/LevelMetersWindow.cpp
        Source/TestCaseCapture.cpp
        Source/TestCaseLoader.cpp
        Source/RestoreTestcaseStateDialog.cpp
        Source/PluginAudioEngine.cpp
        Source/MonitorOutputBridge.cpp
        Source/HardwareLoopOps.cpp
        Source/RemoteTransport.cpp
        Source/RemoteSetupDialog.cpp
        Source/MidiHostServices.cpp
        Source/HostClockMetronome.cpp
        Source/OfflineCapture.cpp
        Source/SessionSnap.cpp
        Source/PluginScanLog.cpp
        Source/AuPluginScanner.cpp
        Source/AuPluginScanDialog.cpp
        Source/PluginScannerOOP.cpp
        Source/SettingsDialog.cpp
        Source/InstallSourceClipsDialog.cpp
        Source/AudioUnitSettingsDialog.cpp
        Source/AddPluginDialog.cpp
        Source/AboutDialog.cpp
        Source/LightsOutManager.cpp
        Source/HardwareAudioSetupDialog.cpp
        Source/MidiSetupDialog.cpp
        Source/MidiEndpointInfo.cpp
        Source/sysex/SysexDeviceRegistry.cpp
        Source/sysex/QuadraverbSysex.cpp
        Source/sysex/DpProSysex.cpp
        Source/SendPluginSettingsToDevice.cpp
        ${QV_SRC}/domain/AlesisCodec.cpp
        ${QV_SRC}/domain/QuadraverbProgram.cpp
        ${QV_SRC}/formats/Qdv1StateIO.cpp
        ${AUFX_GEN_DIR}/AppVersion.cpp
        ../plugin_renderer/Source/AUpresetLoader.cpp)

set_source_files_properties("${AUFX_GEN_DIR}/AppVersion.cpp" "${AUFX_GEN_DIR}/AppVersion.h"
    PROPERTIES GENERATED TRUE)

# Regenerate AppVersion.h/.cpp from VERSION (does not increment; packaging bumps).
add_custom_target(plugin_host_app_version
    COMMAND ${CMAKE_COMMAND}
        -DAUFX_VERSION_FILE=${AUFX_VERSION_FILE}
        -DAUFX_GEN_DIR=${AUFX_GEN_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BumpVersion.cmake
    BYPRODUCTS
        ${AUFX_GEN_DIR}/AppVersion.h
        ${AUFX_GEN_DIR}/AppVersion.cpp
    COMMENT "Generating AU Effects Explorer version headers"
    VERBATIM)
add_dependencies(plugin_host_app plugin_host_app_version)

# Bundle default config into Contents/Resources/
target_sources(plugin_host_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Resources/host.config.json")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/Resources/host.config.json"
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

# Bundle fixture WAVs from the repo fixtures/ directory (subfolders preserved —
# the host shows them as submenus in the Source Clip dropdown). Sync the whole
# tree on each build so removed/moved files don't linger in the .app bundle.
set(AUFX_FIXTURES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures")
add_custom_command(TARGET plugin_host_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_BUNDLE_DIR:plugin_host_app>/Contents/Resources/fixtures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${AUFX_FIXTURES_DIR}"
        "$<TARGET_BUNDLE_DIR:plugin_host_app>/Contents/Resources/fixtures"
    COMMENT "Syncing fixtures into app bundle"
    VERBATIM)

target_compile_definitions(plugin_host_app
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(plugin_host_app
    PRIVATE
        plugin_host_app_assets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_data_structures
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(APPLE)
    target_sources(plugin_host_app PRIVATE Source/AUpresetSaver_mac.mm)
    target_sources(plugin_host_app PRIVATE Source/HostPreferences_mac.mm)
    target_sources(plugin_host_app PRIVATE Source/LightsOutManager.mm)
    set_source_files_properties(Source/LightsOutManager.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_sources(plugin_host_app PRIVATE Source/MidiEndpointInfo_mac.mm)
    set_source_files_properties(Source/MidiEndpointInfo_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_sources(plugin_host_app PRIVATE Source/PluginScannerWorker_mac.mm)
    target_sources(plugin_host_app PRIVATE ../plugin_renderer/Source/AUpresetLoader_mac.mm)
    target_link_libraries(plugin_host_app PRIVATE
        "-framework AppKit"
        "-framework Foundation"
        "-framework CoreFoundation"
        "-framework CoreMIDI")
else()
    target_sources(plugin_host_app PRIVATE Source/AUpresetSaver.cpp)
    target_compile_definitions(plugin_host_app PRIVATE JUCE_PLUGIN_HOST_NO_MAC_PRESET=1)
    target_compile_definitions(plugin_host_app PRIVATE JUCE_PLUGIN_RENDERER_NO_MAC_PRESET=1)
endif()

juce_generate_juce_header(plugin_host_app)
