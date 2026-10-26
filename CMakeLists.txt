cmake_minimum_required(VERSION 3.22)
project(AmapianoHarmony VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
# SOURCE_SUBDIR bidon : on veut seulement télécharger les fichiers (bibliothèque d'en-têtes)
FetchContent_Declare(signalsmith
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG main
    SOURCE_SUBDIR _none_)
FetchContent_MakeAvailable(JUCE signalsmith)

juce_add_plugin(AmapianoHarmony
    COMPANY_NAME "Emmanuel Poudjougo"
    PLUGIN_MANUFACTURER_CODE Epou
    PLUGIN_CODE Amhr
    FORMATS VST3
    PRODUCT_NAME "Amapiano Harmony"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(AmapianoHarmony PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

target_include_directories(AmapianoHarmony PRIVATE ${signalsmith_SOURCE_DIR})

target_compile_definitions(AmapianoHarmony PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AmapianoHarmony
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
