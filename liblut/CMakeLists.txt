set(CMAKE_AUTOMOC ON)

add_library(liblut STATIC
    liblut.cpp
    protocol/events.cpp
    progress/Weights.cpp
    progress/Eta.cpp
    progress/ProgressModel.cpp
    backend/Validation.cpp
    backend/Capabilities.cpp
    backend/Backend.cpp
    backend/replay/ReplayBackend.cpp
    backend/dnf5/Dnf5Backend.cpp
    backend/alpm/AlpmBackend.cpp
    backend/apt/StatusFdParser.cpp
    backend/apt/AptBackend.cpp
    backend/apt/PlanGuard.cpp
    backend/flatpak/FlatpakBackend.cpp
    backend/snap/SnapAvailability.cpp
    backend/snap/SnapBackend.cpp
    backend/pacstall/PacstallBackend.cpp
    detect/DistroDetect.cpp
    history/HistoryDb.cpp
    transaction/TransactionTypes.cpp
    catalog/PackageCatalog.cpp
    catalog/alpm/AlpmPackageCatalog.cpp
    catalog/dnf5/Dnf5PackageCatalog.cpp
    catalog/apt/AptPackageCatalog.cpp
    catalog/flatpak/FlatpakPackageCatalog.cpp
    catalog/snap/SnapPackageCatalog.cpp
    repository/RepoManager.cpp
)

target_include_directories(liblut PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(liblut PUBLIC
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    SQLite3::SQLite3
)

# Einzige Versionsquelle ist project() im Wurzel-CMakeLists.txt.
target_compile_definitions(liblut PRIVATE LUT_VERSION="${PROJECT_VERSION}")

if(ALPM_FOUND)
    target_compile_definitions(liblut PUBLIC HAVE_ALPM=1)
    target_include_directories(liblut PUBLIC ${ALPM_INCLUDE_DIRS})
    target_link_libraries(liblut PUBLIC ${ALPM_LIBRARIES})

    add_executable(lut-alpm-worker
        backend/alpm/worker_main.cpp
    )
    target_link_libraries(lut-alpm-worker PRIVATE
        liblut
        Qt6::Core
        ${ALPM_LIBRARIES}
    )
    target_include_directories(lut-alpm-worker PRIVATE
        ${ALPM_INCLUDE_DIRS}
    )
    install(TARGETS lut-alpm-worker DESTINATION /usr/libexec/linux-update-tool)
endif()

add_executable(lut-apt-guard backend/apt/guard_main.cpp)
target_link_libraries(lut-apt-guard PRIVATE liblut Qt6::Core)
install(TARGETS lut-apt-guard DESTINATION /usr/libexec/linux-update-tool)
