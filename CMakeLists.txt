cmake_minimum_required(VERSION 3.24)
project(MetamorphRebuild VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout")
if(JUCE_SOURCE_DIR)
  add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
  FetchContent_MakeAvailable(JUCE)
endif()
juce_add_plugin(MetamorphRebuild
  COMPANY_NAME "Metamorph Rebuild Contributors"
  BUNDLE_ID "org.metamorph-rebuild.vst3"
  PLUGIN_MANUFACTURER_CODE Mrph PLUGIN_CODE Mrb1
  FORMATS VST3 PRODUCT_NAME "Metamorph Rebuild"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx)
target_sources(MetamorphRebuild PRIVATE src/Processor.cpp src/Editor.cpp)
target_compile_definitions(MetamorphRebuild PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MetamorphRebuild PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
juce_generate_juce_header(MetamorphRebuild)
option(MR_BUILD_TESTS "Build integration tests" ON)
if(MR_BUILD_TESTS)
  enable_testing()
  juce_add_console_app(MetamorphTests PRODUCT_NAME "Metamorph Tests")
  target_sources(MetamorphTests PRIVATE tests/plugin_test.cpp)
  target_link_libraries(MetamorphTests PRIVATE MetamorphRebuild)
  target_include_directories(MetamorphTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/MetamorphRebuild_artefacts/JuceLibraryCode")
  target_include_directories(MetamorphTests PRIVATE "$<TARGET_PROPERTY:juce::juce_audio_utils,INTERFACE_INCLUDE_DIRECTORIES>")
  add_test(NAME plugin-core COMMAND MetamorphTests)
endif()
