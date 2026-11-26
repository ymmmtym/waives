# Helpers shared by every plugin in this repository. A plugin directory only
# declares what makes it different; formats, warning flags and the headless test
# harness are configured once, here.

set(WAIVES_PLUGIN_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

# waives_add_plugin(<target>
#     COMPANY_NAME <name> BUNDLE_ID <id>
#     MANUFACTURER_CODE <XXXX> PLUGIN_CODE <XXXX>
#     DESCRIPTION <text>
#     [CLAP_ID <id> CLAP_FEATURES <feature>...]
#     SOURCES <file>...
#     [LINK_LIBRARIES <lib>...])
#
# PLUGIN_CODE and MANUFACTURER_CODE must be unique per plugin: `auval -v aufx
# <PLUGIN_CODE> <MANUFACTURER_CODE>` is how Logic identifies the component.
function(waives_add_plugin target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG
        ""
        "COMPANY_NAME;BUNDLE_ID;MANUFACTURER_CODE;PLUGIN_CODE;DESCRIPTION;CLAP_ID;CLAP_FEATURES"
        "SOURCES;LINK_LIBRARIES")

    foreach(required COMPANY_NAME BUNDLE_ID MANUFACTURER_CODE PLUGIN_CODE SOURCES)
        if(NOT ARG_${required})
            message(FATAL_ERROR "waives_add_plugin(${target}): ${required} is required")
        endif()
    endforeach()

    get_property(claimed GLOBAL PROPERTY waives_claimed_plugin_codes)

    if("${ARG_MANUFACTURER_CODE}:${ARG_PLUGIN_CODE}" IN_LIST claimed)
        message(FATAL_ERROR
            "waives_add_plugin(${target}): ${ARG_MANUFACTURER_CODE}:${ARG_PLUGIN_CODE} "
            "is already used by another plugin")
    endif()

    set_property(GLOBAL APPEND PROPERTY waives_claimed_plugin_codes
        "${ARG_MANUFACTURER_CODE}:${ARG_PLUGIN_CODE}")

    # AU can only be built by Apple's toolchain; elsewhere the same sources
    # produce VST3 + CLAP + Standalone.
    set(formats VST3 Standalone)
    if(APPLE)
        list(APPEND formats AU)
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME                 "${ARG_COMPANY_NAME}"
        BUNDLE_ID                    "${ARG_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE     ${ARG_MANUFACTURER_CODE}
        PLUGIN_CODE                  ${ARG_PLUGIN_CODE}
        PRODUCT_NAME                 "${target}"
        DESCRIPTION                  "${ARG_DESCRIPTION}"
        VERSION                      "${PROJECT_VERSION}"
        IS_SYNTH                     FALSE
        NEEDS_MIDI_INPUT             FALSE
        NEEDS_MIDI_OUTPUT            FALSE
        IS_MIDI_EFFECT               FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS  FALSE
        COPY_PLUGIN_AFTER_BUILD      FALSE
        FORMATS                      ${formats})

    target_sources(${target} PRIVATE ${ARG_SOURCES})
    target_compile_definitions(${target} PUBLIC ${WAIVES_PLUGIN_DEFINITIONS})

    # Filter and noise helpers shared by every plugin (shared/WaivesDsp.h).
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/shared")

    target_link_libraries(${target}
        PRIVATE ${ARG_LINK_LIBRARIES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # The workflow validates the AU with `auval -v <type> <code> <manufacturer>`.
    # Writing the codes here keeps them defined in exactly one place.
    file(WRITE "${CMAKE_BINARY_DIR}/au_code_${target}.txt"
        "aufx ${ARG_PLUGIN_CODE} ${ARG_MANUFACTURER_CODE}\n")

    if(WAIVES_BUILD_CLAP AND ARG_CLAP_ID)
        clap_juce_extensions_plugin(TARGET ${target}
            CLAP_ID       "${ARG_CLAP_ID}"
            CLAP_FEATURES ${ARG_CLAP_FEATURES})
    endif()
endfunction()

# waives_add_plugin_tests(<target>
#     TEST_SOURCE <file>
#     SOURCES <plugin source>...
#     INCLUDE_DIRS <dir>...
#     [LINK_LIBRARIES <lib>...])
#
# Builds a console executable that drives the real AudioProcessor without a host
# or an audio device, and registers it with ctest.
function(waives_add_plugin_tests target)
    if(NOT WAIVES_BUILD_TESTS)
        return()
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 ARG
        "" "TEST_SOURCE" "SOURCES;INCLUDE_DIRS;LINK_LIBRARIES")

    if(NOT ARG_TEST_SOURCE)
        message(FATAL_ERROR "waives_add_plugin_tests(${target}): TEST_SOURCE is required")
    endif()

    juce_add_console_app(${target} PRODUCT_NAME "${target}")

    target_sources(${target} PRIVATE ${ARG_TEST_SOURCE} ${ARG_SOURCES})

    target_include_directories(${target} PRIVATE
        ${ARG_INCLUDE_DIRS}
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/shared")

    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(${target}
        PRIVATE ${ARG_LINK_LIBRARIES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME ${target} COMMAND ${target})
endfunction()

# waives_add_plugin_benchmark(<target>
#     BENCHMARK_SOURCE <file>
#     SOURCES <plugin source>...
#     INCLUDE_DIRS <dir>...
#     [LINK_LIBRARIES <lib>...])
#
# Builds a console executable that reports processing cost. Deliberately not
# registered with ctest: the numbers depend on the machine, so they are recorded
# in the README instead of being asserted.
function(waives_add_plugin_benchmark target)
    if(NOT WAIVES_BUILD_BENCHMARKS)
        return()
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 ARG
        "" "BENCHMARK_SOURCE" "SOURCES;INCLUDE_DIRS;LINK_LIBRARIES")

    if(NOT ARG_BENCHMARK_SOURCE)
        message(FATAL_ERROR "waives_add_plugin_benchmark(${target}): BENCHMARK_SOURCE is required")
    endif()

    juce_add_console_app(${target} PRODUCT_NAME "${target}")

    target_sources(${target} PRIVATE ${ARG_BENCHMARK_SOURCE} ${ARG_SOURCES})

    target_include_directories(${target} PRIVATE
        ${ARG_INCLUDE_DIRS}
        "${PROJECT_SOURCE_DIR}/tests"
        "${PROJECT_SOURCE_DIR}/shared")

    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(${target}
        PRIVATE ${ARG_LINK_LIBRARIES}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()
