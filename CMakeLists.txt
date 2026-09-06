cmake_minimum_required(VERSION 3.20)
project(NumberMunchersNative VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(NumberMunchers WIN32
    src/main.cpp
    src/audio_player.cpp
    src/assets.cpp
    src/dro_audio.cpp
    src/mecc_sound.cpp
    src/munchers_app.cpp
    src/muncher_score.cpp
    src/opl_stream_player.cpp
    src/scene_script.cpp
    src/super_board.cpp
    src/super_config.cpp
    src/super_game.cpp
    src/super_runtime.cpp
    src/super_scene_callbacks.cpp
    src/super_content.cpp
    src/word_config.cpp
    src/word_content.cpp
    src/word_game.cpp
    src/word_runtime.cpp
    src/render.cpp
    src/game.cpp
    third_party/ymfm/src/ymfm_adpcm.cpp
    third_party/ymfm/src/ymfm_opl.cpp
    third_party/ymfm/src/ymfm_pcm.cpp
    src/resources.rc
)

target_include_directories(NumberMunchers PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src>)

target_compile_definitions(NumberMunchers PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MINGW)
    target_compile_options(NumberMunchers PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(NumberMunchers PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(NumberMunchers PRIVATE gdi32 winmm shell32)

set_target_properties(NumberMunchers PROPERTIES
    OUTPUT_NAME "Number Munchers"
)

install(TARGETS NumberMunchers RUNTIME DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    add_executable(NumberMunchersRenderTests
        tests/game_render_test.cpp
        src/audio_player.cpp
        src/assets.cpp
        src/dro_audio.cpp
        src/mecc_sound.cpp
        src/muncher_score.cpp
        src/opl_stream_player.cpp
        src/scene_script.cpp
        src/super_scene_callbacks.cpp
        src/render.cpp
        src/game.cpp
        third_party/ymfm/src/ymfm_adpcm.cpp
        third_party/ymfm/src/ymfm_opl.cpp
        third_party/ymfm/src/ymfm_pcm.cpp
        src/resources.rc
    )
    target_include_directories(NumberMunchersRenderTests PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src>)
    target_compile_definitions(NumberMunchersRenderTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX NUMBER_MUNCHERS_TESTING=1)
    if(MINGW)
        target_compile_options(NumberMunchersRenderTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(NumberMunchersRenderTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_libraries(NumberMunchersRenderTests PRIVATE gdi32 winmm shell32)
    add_test(NAME game_render_state_test COMMAND NumberMunchersRenderTests)

    add_executable(WordContentTests
        tests/word_content_test.cpp
        tests/word_content_resources.rc
        src/assets.cpp
        src/dro_audio.cpp
        src/mecc_sound.cpp
        src/muncher_score.cpp
        src/scene_script.cpp
        src/word_config.cpp
        src/word_content.cpp
        src/word_game.cpp
        third_party/ymfm/src/ymfm_adpcm.cpp
        third_party/ymfm/src/ymfm_opl.cpp
        third_party/ymfm/src/ymfm_pcm.cpp
    )
    target_include_directories(WordContentTests PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src>)
    target_compile_definitions(WordContentTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        target_compile_options(WordContentTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(WordContentTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME word_content_static_test COMMAND WordContentTests)

    add_executable(SuperContentTests
        tests/super_content_test.cpp
        src/assets.cpp
        src/muncher_score.cpp
        src/super_board.cpp
        src/super_config.cpp
        src/super_content.cpp
        src/super_game.cpp
        src/resources.rc
    )
    target_compile_definitions(SuperContentTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        target_compile_options(SuperContentTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(SuperContentTests PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME super_content_static_test COMMAND SuperContentTests)

    add_executable(SuperScriptTests
        tests/super_script_test.cpp
        src/assets.cpp
        src/scene_script.cpp
        src/super_scene_callbacks.cpp
        src/resources.rc
    )
    target_compile_definitions(SuperScriptTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        target_compile_options(SuperScriptTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(SuperScriptTests PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME super_script_static_test COMMAND SuperScriptTests)

    add_executable(MunchersAppTests
        tests/munchers_app_test.cpp
        src/audio_player.cpp
        src/assets.cpp
        src/dro_audio.cpp
        src/mecc_sound.cpp
        src/munchers_app.cpp
        src/muncher_score.cpp
        src/opl_stream_player.cpp
        src/scene_script.cpp
        src/super_scene_callbacks.cpp
        src/super_board.cpp
        src/super_config.cpp
        src/super_content.cpp
        src/super_game.cpp
        src/super_runtime.cpp
        src/word_config.cpp
        src/word_content.cpp
        src/word_game.cpp
        src/word_runtime.cpp
        src/render.cpp
        src/game.cpp
        third_party/ymfm/src/ymfm_adpcm.cpp
        third_party/ymfm/src/ymfm_opl.cpp
        third_party/ymfm/src/ymfm_pcm.cpp
        src/resources.rc
    )
    target_include_directories(MunchersAppTests PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src>)
    target_compile_definitions(MunchersAppTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX NUMBER_MUNCHERS_TESTING=1)
    if(MINGW)
        target_compile_options(MunchersAppTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(MunchersAppTests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_libraries(MunchersAppTests PRIVATE gdi32 winmm shell32)
    add_test(NAME munchers_app_headless_test COMMAND MunchersAppTests)

    add_executable(NumberMunchersAudioProbe
        tools/render_number_gsnd.cpp
        src/assets.cpp
        src/dro_audio.cpp
        src/mecc_sound.cpp
        third_party/ymfm/src/ymfm_adpcm.cpp
        third_party/ymfm/src/ymfm_opl.cpp
        third_party/ymfm/src/ymfm_pcm.cpp
        src/resources.rc
    )
    target_include_directories(NumberMunchersAudioProbe PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src>)
    target_compile_definitions(NumberMunchersAudioProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        target_compile_options(NumberMunchersAudioProbe PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(NumberMunchersAudioProbe PRIVATE
            -static -static-libgcc -static-libstdc++)
    endif()

    add_executable(ProductionArtifactTests
        tests/production_artifact_test.cpp
    )
    target_compile_definitions(ProductionArtifactTests PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MINGW)
        target_compile_options(ProductionArtifactTests PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(ProductionArtifactTests PRIVATE
            -municode -static -static-libgcc -static-libstdc++)
    endif()
    add_dependencies(ProductionArtifactTests NumberMunchers)
    add_test(NAME production_artifact_self_containment_test
             COMMAND ProductionArtifactTests "$<TARGET_FILE:NumberMunchers>")
endif()
