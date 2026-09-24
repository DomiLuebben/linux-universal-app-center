# Unit tests
add_subdirectory(unit)

add_executable(dnf5_integration integration/dnf5_integration.cpp)
target_link_libraries(dnf5_integration PRIVATE liblut Qt6::Core)

add_executable(apt_integration integration/apt_integration.cpp)
target_link_libraries(apt_integration PRIVATE liblut Qt6::Core)

# Treibt FlatpakBackend und FlatpakPackageCatalog gegen eine echte flatpak-CLI.
# Läuft nur im Wegwerf-Container, siehe scripts/verify-flatpak.sh.
add_executable(flatpak_integration integration/flatpak_integration.cpp)
target_link_libraries(flatpak_integration PRIVATE liblut Qt6::Core)

# Treibt PacstallBackend gegen die echte pacstall-CLI und das offizielle PPR-Repository.
# Läuft nur im Wegwerf-Container, siehe scripts/verify-pacstall.sh.
add_executable(pacstall_integration integration/pacstall_integration.cpp)
target_link_libraries(pacstall_integration PRIVATE liblut Qt6::Core)


