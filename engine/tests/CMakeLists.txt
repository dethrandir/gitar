add_executable(gitar_engine_tests
    main.cpp
    test_ring_buffer.cpp
    test_level_meter.cpp
    test_bridge.cpp
    test_processor.cpp
    test_engine.cpp
    test_control_server.cpp
    test_cli.cpp)
target_link_libraries(gitar_engine_tests PRIVATE gitar_engine gitar_doctest gitar_json)
gitar_set_warnings(gitar_engine_tests)

add_test(NAME engine_tests COMMAND gitar_engine_tests)
