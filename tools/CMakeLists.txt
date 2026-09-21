set(CMAKE_AUTOMOC ON)

add_executable(lut-replay
    replay_main.cpp
)

target_link_libraries(lut-replay PRIVATE
    liblut
    Qt6::Core
)
