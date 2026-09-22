# Unit tests
add_subdirectory(unit)

add_executable(dnf5_integration integration/dnf5_integration.cpp)
target_link_libraries(dnf5_integration PRIVATE liblut Qt6::Core)

add_executable(apt_integration integration/apt_integration.cpp)
target_link_libraries(apt_integration PRIVATE liblut Qt6::Core)

