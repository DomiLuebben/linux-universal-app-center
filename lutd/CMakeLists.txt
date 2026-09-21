set(CMAKE_AUTOMOC ON)

add_executable(lutd
    main.cpp
)

target_link_libraries(lutd PRIVATE
    liblut
    Qt6::Core
    Qt6::DBus
    PolkitQt6-1::Core
)

install(TARGETS lutd DESTINATION /usr/libexec)
