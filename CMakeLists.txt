cmake_minimum_required(VERSION 3.22)

project(MeridianRack VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build a universal binary (Apple Silicon + Intel) on macOS.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_DIR is given, otherwise download it.
# ---------------------------------------------------------------------------
set(MERIDIAN_JUCE_TAG "9.0.4" CACHE STRING "JUCE version to download")

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${MERIDIAN_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(MERIDIAN_BUILD_TESTS "Build the offline test runner (used for CI / Linux checks)" OFF)
option(MERIDIAN_HEADLESS    "Disable optional X11 extensions (Linux test builds only)" OFF)

set(MERIDIAN_FORMATS AU VST3 Standalone)
if(NOT APPLE)
    set(MERIDIAN_FORMATS VST3)
endif()

set(MERIDIAN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/Presets.cpp
    Source/dsp/SpectralUnmasker.cpp
    Source/dsp/CharacterChain.cpp
    Source/dsp/Limiter.cpp
    Source/dsp/LoudnessMeter.cpp
    Source/dsp/ReferenceTrack.cpp
    Source/ui/RackLookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/EqDisplay.cpp
    Source/ui/Pages.cpp)

set(MERIDIAN_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

if(MERIDIAN_HEADLESS)
    list(APPEND MERIDIAN_DEFS
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
        JUCE_USE_XINPUT=0 JUCE_USE_XSHM=0)
endif()

if(NOT MERIDIAN_BUILD_TESTS)
juce_add_plugin(MeridianRack
    COMPANY_NAME                "Meridian Audio Lab"
    BUNDLE_ID                   "com.meridianaudiolab.meridianrack"
    PLUGIN_MANUFACTURER_CODE    Mrdn
    PLUGIN_CODE                 MrRk
    PRODUCT_NAME                "Meridian Rack"
    FORMATS                     ${MERIDIAN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Mastering EQ Dynamics
    HARDENED_RUNTIME_ENABLED    TRUE)

target_sources(MeridianRack PRIVATE ${MERIDIAN_SOURCES})
target_include_directories(MeridianRack PRIVATE Source)
target_compile_definitions(MeridianRack PUBLIC ${MERIDIAN_DEFS})
target_link_libraries(MeridianRack
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Offline test runner: processes test signals, checks loudness accuracy,
# latency alignment and renders the editor to PNG files.
# ---------------------------------------------------------------------------
if(MERIDIAN_BUILD_TESTS)
    juce_add_console_app(MeridianTests PRODUCT_NAME "MeridianTests")
    target_sources(MeridianTests PRIVATE tests/TestMain.cpp ${MERIDIAN_SOURCES})
    target_include_directories(MeridianTests PRIVATE Source)
    target_compile_definitions(MeridianTests PRIVATE
        ${MERIDIAN_DEFS}
        JucePlugin_Name="Meridian Rack"
        JucePlugin_IsSynth=0
        JucePlugin_WantsMidiInput=0
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_VersionString="1.0.0"
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(MeridianTests PRIVATE
        juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
        juce::juce_gui_extra juce::juce_recommended_config_flags)
endif()
