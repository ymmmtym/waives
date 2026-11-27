# Tape machine: wow/flutter, tape saturation, head bump and hiss. Everything
# shared (formats, warning flags, the test harness) lives in cmake/WaivesPlugin.cmake.

set(TAPE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

waives_add_plugin(MyFirstTapeMachine
    COMPANY_NAME      "MyAudioLab"
    BUNDLE_ID         "com.myaudiolab.myfirsttapemachine"
    MANUFACTURER_CODE Myau
    PLUGIN_CODE       Mftm
    DESCRIPTION       "Tape machine: saturation, wow and flutter, hiss"
    CLAP_ID           "com.myaudiolab.myfirsttapemachine"
    CLAP_FEATURES     audio-effect distortion stereo
    SOURCES           ${TAPE_SOURCES}
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)

waives_add_plugin_tests(MyFirstTapeMachineTests
    TEST_SOURCE  tests/ProcessorTest.cpp
    SOURCES      ${TAPE_SOURCES}
    INCLUDE_DIRS Source
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)
