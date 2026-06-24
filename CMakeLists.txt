cmake_minimum_required(VERSION 3.22)
project(MantisVexQ VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("D:/Juce/JUCE" "${CMAKE_BINARY_DIR}/JUCE")

juce_add_plugin(MantisVexQ
    COMPANY_NAME                "MantisVex"
    PLUGIN_MANUFACTURER_CODE    Mntx
    PLUGIN_CODE                 MvxQ
    FORMATS                     VST3
    PRODUCT_NAME                "Mantis Vex Q"
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_SYNTH                    FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Fx|EQ"
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

target_sources(MantisVexQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/EQBand.cpp
    Source/DSP/DynBand.cpp
    Source/GUI/EQDisplay.cpp
)

target_compile_definitions(MantisVexQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

juce_add_binary_data(MantisVexQ_Resources SOURCES
    Source/Resources/Orbitron-Black.ttf
)
set_target_properties(MantisVexQ_Resources PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_generate_juce_header(MantisVexQ)

target_link_libraries(MantisVexQ PRIVATE
    MantisVexQ_Resources
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
