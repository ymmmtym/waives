# Single-band peaking EQ. Everything shared (formats, warning flags, the test
# harness) lives in cmake/WaivesPlugin.cmake.

set(MYEQ_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/FrequencyResponseCurve.cpp)

waives_add_plugin(MyFirstMasteringEQ
    COMPANY_NAME      "MyAudioLab"
    BUNDLE_ID         "com.myaudiolab.myfirstmasteringeq"
    MANUFACTURER_CODE Myau
    PLUGIN_CODE       Mfmq
    DESCRIPTION       "Single-band peaking EQ for mastering"
    CLAP_ID           "com.myaudiolab.myfirstmasteringeq"
    CLAP_FEATURES     audio-effect equalizer stereo
    SOURCES           ${MYEQ_SOURCES}
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)

waives_add_plugin_tests(MyFirstMasteringEQTests
    TEST_SOURCE  tests/ProcessorTest.cpp
    SOURCES      ${MYEQ_SOURCES}
    INCLUDE_DIRS Source
    LINK_LIBRARIES
        juce::juce_audio_utils
        juce::juce_dsp)
