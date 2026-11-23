# Third-party dependencies, pinned for reproducible builds.
#
# JUCE is downloaded once into .cache/fetchcontent and reused by every build
# directory, so deleting build/ does not trigger a re-download.

# Must be set before include(FetchContent): that module assigns the default
# (${CMAKE_BINARY_DIR}/_deps) to this cache variable when it is first included.
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.cache/fetchcontent" CACHE PATH
    "Shared download/cache root for FetchContent")

include(FetchContent)

set(JUCE_VERSION "9.0.6")
set(JUCE_SHA256 "103e6c9d5f17f19bc7b5485a890573609d326513aad4db6b045181f4936ef424")

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)

FetchContent_Declare(JUCE
    URL      "https://github.com/juce-framework/JUCE/archive/refs/tags/${JUCE_VERSION}.tar.gz"
    URL_HASH "SHA256=${JUCE_SHA256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# JUCE has no built-in CLAP exporter; clap-juce-extensions adds one on top of
# any juce_add_plugin() target. Pinned to a commit because it publishes no tags.
set(CLAP_JUCE_EXTENSIONS_COMMIT "3837f4a6ed2ab0ba46aba69ae5b7041c7c64ce74")

FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY "https://github.com/free-audio/clap-juce-extensions.git"
    GIT_TAG        "${CLAP_JUCE_EXTENSIONS_COMMIT}"
    GIT_PROGRESS   TRUE)

FetchContent_MakeAvailable(JUCE clap_juce_extensions)
