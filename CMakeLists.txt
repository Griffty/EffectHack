cmake_minimum_required(VERSION 3.22)
project(EffectHack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
# JUCE 8.0.17-security, pinned to an immutable revision and archive checksum.
FetchContent_Declare(JUCE
    URL https://codeload.github.com/juce-framework/JUCE/tar.gz/dabd5ad13cd869cba41046947fffc8e5ce5cd662
    URL_HASH SHA256=302949ee39f8fce798ee2bbd30445f8f31a4ecf4643463c68c54882020e16c13)
FetchContent_MakeAvailable(JUCE)
find_package(CURL 7.85 REQUIRED)

juce_add_gui_app(EffectHack
    PRODUCT_NAME "EffectHack"
    COMPANY_NAME "EffectHack"
    BUNDLE_ID "com.effecthack.processor"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "EffectHack processes the guitar connected to your audio interface.")

target_sources(EffectHack PRIVATE
    main.cpp
    AudioEngine.cpp AudioEngine.h
    InputMonitor.cpp InputMonitor.h
    Tuner.cpp Tuner.h RigTools.cpp RigTools.h RigStatus.cpp RigStatus.h
    MetricMeter.cpp MetricMeter.h
    VirtualInput.cpp VirtualInput.h
    MainComponent.cpp MainComponent.h
    MainComponentSong.cpp SongPresetPanel.cpp SongPresetPanel.h
    ApiConfig.cpp ApiConfig.h HttpClient.cpp HttpClient.h
    YouTubeMetadata.cpp YouTubeMetadata.h GeminiRecommender.cpp GeminiRecommender.h
    ToneAnalysis.cpp ToneAnalysis.h ToneMatching.cpp ToneMatching.h ReferenceAudio.cpp ReferenceAudio.h
    MainComponentSession.cpp SessionState.cpp SessionState.h
    PresetManager.cpp PresetManager.h
    GeneratedTonePreparation.cpp GeneratedTonePreparation.h
    GeneratedRack.cpp GeneratedRack.h RackDefinition.h
    FactoryPresets.cpp FactoryPresets.h
    effects/NoiseGate.cpp effects/NoiseGate.h
    effects/Compressor.cpp effects/Compressor.h
    effects/Amp.cpp effects/Amp.h
    effects/Cabinet.cpp effects/Cabinet.h
    effects/Equalizer.cpp effects/Equalizer.h
    effects/RackStage.cpp effects/RackStage.h
    effects/ChannelWiseRackStage.cpp effects/ChannelWiseRackStage.h
    effects/StereoRackStage.cpp effects/StereoRackStage.h
    effects/Chorus.cpp effects/Chorus.h
    effects/Delay.cpp effects/Delay.h
    effects/Reverb.cpp effects/Reverb.h
    effects/Distortion.cpp effects/Distortion.h)
target_compile_definitions(EffectHack PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:EffectHack,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(EffectHack PRIVATE
    juce::juce_audio_utils juce::juce_dsp CURL::libcurl
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

include(CTest)

# The matcher can be exercised without opening an audio device or the desktop UI.
set(EFFECTHACK_MATCHER_SOURCES
    ToneAnalysis.cpp ToneMatching.cpp ReferenceAudio.cpp YouTubeMetadata.cpp
    GeneratedRack.cpp GeneratedTonePreparation.cpp PresetManager.cpp FactoryPresets.cpp
    AudioEngine.cpp InputMonitor.cpp VirtualInput.cpp
    effects/NoiseGate.cpp effects/Compressor.cpp effects/Amp.cpp effects/Distortion.cpp
    effects/Cabinet.cpp effects/Equalizer.cpp effects/RackStage.cpp
    effects/ChannelWiseRackStage.cpp effects/StereoRackStage.cpp
    effects/Chorus.cpp effects/Delay.cpp effects/Reverb.cpp)
add_executable(EffectHackToneProbe EXCLUDE_FROM_ALL tools/ToneMatchProbe.cpp
    ApiConfig.cpp HttpClient.cpp ${EFFECTHACK_MATCHER_SOURCES})
target_compile_definitions(EffectHackToneProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(EffectHackToneProbe PRIVATE juce::juce_audio_devices juce::juce_dsp CURL::libcurl juce::juce_recommended_config_flags)

if(BUILD_TESTING)
    add_executable(EffectHackMatchingTests tests/ToneMatchingTests.cpp ${EFFECTHACK_MATCHER_SOURCES})
    target_compile_definitions(EffectHackMatchingTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackMatchingTests PRIVATE juce::juce_audio_devices juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME audio_tone_matching COMMAND EffectHackMatchingTests "${CMAKE_CURRENT_BINARY_DIR}/test-matching")
    add_executable(EffectHackSongTests tests/SongRecommendationTests.cpp
        ApiConfig.cpp HttpClient.cpp YouTubeMetadata.cpp GeminiRecommender.cpp GeneratedRack.cpp)
    target_compile_definitions(EffectHackSongTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EffectHackSongTests PRIVATE juce::juce_core CURL::libcurl juce::juce_recommended_config_flags)
    add_test(NAME song_recommendation COMMAND EffectHackSongTests)
    add_executable(EffectHackRigToolsTests tests/TunerTests.cpp tests/InputMonitorTests.cpp
        Tuner.cpp InputMonitor.cpp RigStatus.cpp)
    target_compile_definitions(EffectHackRigToolsTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackRigToolsTests PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)
    add_test(NAME tuner_input_performance COMMAND EffectHackRigToolsTests)
    add_executable(EffectHackDspTests tests/DistortionTests.cpp effects/Distortion.cpp)
    target_compile_definitions(EffectHackDspTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackDspTests PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME distortion_dsp COMMAND EffectHackDspTests)
    add_executable(EffectHackSourceTests tests/VirtualInputTests.cpp VirtualInput.cpp AudioEngine.cpp InputMonitor.cpp
        effects/NoiseGate.cpp effects/Compressor.cpp effects/Amp.cpp effects/Distortion.cpp effects/Cabinet.cpp effects/Equalizer.cpp
        effects/RackStage.cpp effects/ChannelWiseRackStage.cpp effects/StereoRackStage.cpp
        effects/Chorus.cpp effects/Delay.cpp effects/Reverb.cpp)
    target_compile_definitions(EffectHackSourceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackSourceTests PRIVATE juce::juce_audio_devices juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME virtual_input COMMAND EffectHackSourceTests "${CMAKE_CURRENT_BINARY_DIR}/test-audio"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sine-440hz.mp3")
    add_executable(EffectHackToneTests tests/ToneTests.cpp tests/AmpTests.cpp tests/NoiseGateTests.cpp tests/CompressorTests.cpp AudioEngine.cpp InputMonitor.cpp VirtualInput.cpp
        effects/NoiseGate.cpp effects/Compressor.cpp effects/Amp.cpp effects/Distortion.cpp effects/Cabinet.cpp effects/Equalizer.cpp
        effects/RackStage.cpp effects/ChannelWiseRackStage.cpp effects/StereoRackStage.cpp
        effects/Chorus.cpp effects/Delay.cpp effects/Reverb.cpp)
    target_compile_definitions(EffectHackToneTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackToneTests PRIVATE juce::juce_audio_devices juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME cabinet_eq COMMAND EffectHackToneTests "${CMAKE_CURRENT_BINARY_DIR}/test-audio")
    add_executable(EffectHackSpatialTests tests/SpatialEffectsTests.cpp AudioEngine.cpp InputMonitor.cpp VirtualInput.cpp
        effects/NoiseGate.cpp effects/Compressor.cpp effects/Amp.cpp effects/Distortion.cpp effects/Cabinet.cpp effects/Equalizer.cpp
        effects/RackStage.cpp effects/ChannelWiseRackStage.cpp effects/StereoRackStage.cpp
        effects/Chorus.cpp effects/Delay.cpp effects/Reverb.cpp)
    target_compile_definitions(EffectHackSpatialTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackSpatialTests PRIVATE juce::juce_audio_devices juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME chorus_delay_reverb COMMAND EffectHackSpatialTests)
    add_executable(EffectHackPresetTests tests/PresetTests.cpp tests/RackOrderTests.cpp tests/FactoryPresetTests.cpp tests/SessionStateTests.cpp tests/GeneratedToneTests.cpp GeneratedTonePreparation.cpp GeneratedRack.cpp SessionState.cpp PresetManager.cpp FactoryPresets.cpp AudioEngine.cpp InputMonitor.cpp VirtualInput.cpp
        effects/NoiseGate.cpp effects/Compressor.cpp effects/Amp.cpp effects/Distortion.cpp effects/Cabinet.cpp effects/Equalizer.cpp
        effects/RackStage.cpp effects/ChannelWiseRackStage.cpp effects/StereoRackStage.cpp
        effects/Chorus.cpp effects/Delay.cpp effects/Reverb.cpp)
    target_compile_definitions(EffectHackPresetTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackPresetTests PRIVATE juce::juce_audio_devices juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME presets COMMAND EffectHackPresetTests "${CMAKE_CURRENT_BINARY_DIR}/test-presets")
    add_executable(EffectHackMasterTests tests/MasterTests.cpp tests/InputToolsEngineTests.cpp PresetManager.cpp FactoryPresets.cpp AudioEngine.cpp InputMonitor.cpp VirtualInput.cpp
        effects/NoiseGate.cpp effects/Compressor.cpp effects/Amp.cpp effects/Distortion.cpp effects/Cabinet.cpp effects/Equalizer.cpp
        effects/RackStage.cpp effects/ChannelWiseRackStage.cpp effects/StereoRackStage.cpp
        effects/Chorus.cpp effects/Delay.cpp effects/Reverb.cpp)
    target_compile_definitions(EffectHackMasterTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(EffectHackMasterTests PRIVATE juce::juce_audio_devices juce::juce_dsp juce::juce_recommended_config_flags)
    add_test(NAME master_playback COMMAND EffectHackMasterTests)
endif()
