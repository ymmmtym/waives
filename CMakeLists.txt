cmake_minimum_required(VERSION 3.22)

project(MyFirstMasteringEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MYEQ_BUILD_CLAP  "Build the CLAP format" ON)
option(MYEQ_BUILD_TESTS "Build the headless processor tests" ON)

include(cmake/Dependencies.cmake)

# AU is an Apple-only format; on Linux the same sources build VST3 + CLAP.
set(MYEQ_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MYEQ_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(MyFirstMasteringEQ
    COMPANY_NAME                 "MyAudioLab"
    BUNDLE_ID                    "com.myaudiolab.myfirstmasteringeq"
    PLUGIN_MANUFACTURER_CODE     Myau
    PLUGIN_CODE                  Mfmq
    PRODUCT_NAME                 "MyFirstMasteringEQ"
    DESCRIPTION                  "Single-band peaking EQ for mastering"
    IS_SYNTH                     FALSE
    NEEDS_MIDI_INPUT             FALSE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT               FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
    COPY_PLUGIN_AFTER_BUILD      FALSE
    FORMATS                      ${MYEQ_PLUGIN_FORMATS})

target_sources(MyFirstMasteringEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FrequencyResponseCurve.cpp)

target_compile_definitions(MyFirstMasteringEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(MyFirstMasteringEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MYEQ_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET MyFirstMasteringEQ
        CLAP_ID       "com.myaudiolab.myfirstmasteringeq"
        CLAP_FEATURES audio-effect equalizer stereo)
endif()

# Headless test executable: drives the AudioProcessor directly, no host or
# audio device required. Runs the same processBlock() the plugin ships.
if(MYEQ_BUILD_TESTS)
    juce_add_console_app(MyFirstMasteringEQTests
        PRODUCT_NAME "MyFirstMasteringEQTests")

    target_sources(MyFirstMasteringEQTests PRIVATE
        tests/ProcessorTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/FrequencyResponseCurve.cpp)

    target_include_directories(MyFirstMasteringEQTests PRIVATE Source)

    target_compile_definitions(MyFirstMasteringEQTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(MyFirstMasteringEQTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    add_test(NAME processor COMMAND MyFirstMasteringEQTests)
endif()
