juce_add_console_app(plugin_renderer
    PRODUCT_NAME "plugin_renderer"
    VERSION 1.0.0
    PLUGINHOST_AU TRUE
    PLUGINHOST_VST3 TRUE)

target_sources(plugin_renderer
    PRIVATE
        Source/Main.cpp
        Source/CommandLine.cpp
        Source/AUpresetLoader.cpp
        Source/PluginRenderer.cpp
        Source/ParameterHelpers.cpp)

target_compile_definitions(plugin_renderer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(plugin_renderer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(APPLE)
    target_sources(plugin_renderer PRIVATE
        Source/AUpresetLoader_mac.mm
        Source/AUpresetSaver_mac.mm)
    target_link_libraries(plugin_renderer PRIVATE "-framework Foundation")
else()
    target_compile_definitions(plugin_renderer PRIVATE JUCE_PLUGIN_RENDERER_NO_MAC_PRESET=1)
    target_sources(plugin_renderer PRIVATE Source/AUpresetSaver.cpp)
endif()

juce_generate_juce_header(plugin_renderer)
