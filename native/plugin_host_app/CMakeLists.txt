juce_add_gui_app(plugin_host_app
    PRODUCT_NAME "Plugin Host"
    VERSION 1.0.0
    PLUGINHOST_AU TRUE
    PLUGINHOST_VST3 TRUE
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_include_directories(plugin_host_app
    PRIVATE
        Source
        ${CMAKE_CURRENT_SOURCE_DIR}/../plugin_renderer/Source)

target_sources(plugin_host_app
    PRIVATE
        Source/Main.cpp
        Source/HostConfig.cpp
        Source/HostLog.cpp
        Source/MainWindow.cpp
        Source/PluginAudioEngine.cpp
        Source/OfflineCapture.cpp
        ../plugin_renderer/Source/AUpresetLoader.cpp
        ../plugin_renderer/Source/ParameterHelpers.cpp)

target_compile_definitions(plugin_host_app
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(plugin_host_app
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(APPLE)
    target_sources(plugin_host_app PRIVATE Source/AUpresetSaver_mac.mm)
    target_sources(plugin_host_app PRIVATE ../plugin_renderer/Source/AUpresetLoader_mac.mm)
    target_link_libraries(plugin_host_app PRIVATE "-framework Foundation")
else()
    target_sources(plugin_host_app PRIVATE Source/AUpresetSaver.cpp)
    target_compile_definitions(plugin_host_app PRIVATE JUCE_PLUGIN_HOST_NO_MAC_PRESET=1)
    target_compile_definitions(plugin_host_app PRIVATE JUCE_PLUGIN_RENDERER_NO_MAC_PRESET=1)
endif()

juce_generate_juce_header(plugin_host_app)
