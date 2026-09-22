add_executable(gitar_engine_tests
    main.cpp
    test_ring_buffer.cpp
    test_level_meter.cpp
    test_pitch_detector.cpp
    test_spectrum_analyzer.cpp
    test_bridge.cpp
    test_processor.cpp
    test_noise_gate.cpp
    test_eq.cpp
    test_engine.cpp
    test_control_server.cpp
    test_cli.cpp
    test_neural_model.cpp
    test_recorder.cpp
    test_metronome.cpp)
target_link_libraries(gitar_engine_tests PRIVATE gitar_engine gitar_doctest gitar_json)
target_compile_definitions(gitar_engine_tests PRIVATE
    GITAR_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
gitar_set_warnings(gitar_engine_tests)

add_test(NAME engine_tests COMMAND gitar_engine_tests)
set_tests_properties(engine_tests PROPERTIES TIMEOUT 120)
