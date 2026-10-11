cmake_minimum_required(VERSION 3.22)

project(LZDefeedback VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(LZDF_BUILD_PLUGIN "Build the JUCE plugin" ON)

# DSP core simulation tests (no JUCE)
enable_testing()
add_executable(sim_test tests/sim_test.cpp)
target_include_directories(sim_test PRIVATE src)
add_test(NAME sim_test COMMAND sim_test)

if(LZDF_BUILD_PLUGIN)
    add_subdirectory(external/JUCE)

    juce_add_plugin(LZDefeedback
        COMPANY_NAME "Lars Zumpe Medienproduktion"
        BUNDLE_ID "de.zumpelars.lzdefeedback"
        PLUGIN_MANUFACTURER_CODE Lzmp
        PLUGIN_CODE Lzdf
        PRODUCT_NAME "LZ Defeedback"
        FORMATS VST3 AU Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Tools
        AU_MAIN_TYPE kAudioUnitType_Effect)

    target_sources(LZDefeedback PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp)

    target_include_directories(LZDefeedback PRIVATE src)

    target_compile_definitions(LZDefeedback PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(LZDefeedback
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
