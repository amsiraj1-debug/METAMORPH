cmake_minimum_required(VERSION 3.24)
project(MetamorphCleanRoom VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout")
if(JUCE_SOURCE_DIR)
  add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  include(FetchContent)
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850
  )
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(MetamorphRebuild
  COMPANY_NAME "Metamorph Clean Room Contributors"
  BUNDLE_ID "org.metamorph-cleanroom.vst3"
  PLUGIN_MANUFACTURER_CODE Mrph
  PLUGIN_CODE Mrc2
  FORMATS VST3 Standalone
  PRODUCT_NAME "Metamorph CR"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx
)

juce_generate_juce_header(MetamorphRebuild)

target_sources(MetamorphRebuild PRIVATE
  src/Processor.cpp
  src/Processor.h
  src/Editor.cpp
  src/Editor.h
  src/VoiceProfile.cpp
  src/VoiceProfile.h
  src/PitchShifter.h
)

target_compile_definitions(MetamorphRebuild PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MetamorphRebuild PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_formats
  juce::juce_dsp
  juce::juce_gui_extra
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)
