set(CMAKE_AUTOMOC ON)

macro(add_lut_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_compile_definitions(${test_name} PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(${test_name} PRIVATE
        liblut
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_lut_test(weights_test)
add_lut_test(eta_test)
add_lut_test(events_test)
add_lut_test(progress_test)
add_lut_test(validation_test)
add_lut_test(history_test)
add_lut_test(replay_test)
