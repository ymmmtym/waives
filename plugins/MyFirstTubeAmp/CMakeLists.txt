# Valve amp: two non-linear stages, tone stack, supply sag and a 4x12 cabinet.
# Everything shared (formats, warning flags, the test harness) lives in
# cmake/WaivesPlugin.cmake.

set(AMP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

waives_add_plugin(MyFirstTubeAmp
    COMPANY_NAME      "MyAudioLab"
    BUNDLE_ID         "com.myaudiolab.myfirsttubeamp"
    MANUFACTURER_CODE Myau
    PLUGIN_CODE       Mfta
    DESCRIPTION       "Valve amp: two stages, tone stack, sag and cabinet"
    CLAP_ID           "com.myaudiolab.myfirsttubeamp"
    CLAP_FEATURES     audio-effect distortion stereo
    SOURCES           ${AMP_SOURCES}
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)

waives_add_plugin_tests(MyFirstTubeAmpTests
    TEST_SOURCE  tests/ProcessorTest.cpp
    SOURCES      ${AMP_SOURCES}
    INCLUDE_DIRS Source
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)
