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
add_lut_test(distro_detect_test)
add_lut_test(statusfd_test)
if(ALPM_FOUND)
    add_lut_test(alpm_backend_test)
endif()
add_lut_test(siglevel_test)

# Theme & Contrast test (needs Qt6::Gui)
add_executable(theme_contrast_test
    theme_contrast_test.cpp
    ../../linux-update-tool/theme/Contrast.cpp
)
target_include_directories(theme_contrast_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(theme_contrast_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)
add_test(NAME theme_contrast_test COMMAND theme_contrast_test)

# Farbschema aus kdeglobals: QSettings-Falle und Flächenableitung
add_executable(kdeglobals_test
    kdeglobals_test.cpp
    ../../linux-update-tool/theme/SystemPalette.cpp
    ../../linux-update-tool/theme/Contrast.cpp
)
target_include_directories(kdeglobals_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(kdeglobals_test PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Test
)
add_test(NAME kdeglobals_test COMMAND kdeglobals_test)

add_executable(list_perf_test
    list_perf_test.cpp
    ../../linux-update-tool/models/UpdatesModel.cpp
)
target_include_directories(list_perf_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(list_perf_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Test
)
add_test(NAME list_perf_test COMMAND list_perf_test)

add_lut_test(dnf5_backend_test)
add_lut_test(apt_metadata_test)

# Prüft die Ereignis-Verdrahtung des GUI-Clients ohne laufenden Daemon.
add_executable(daemonclient_log_test
    daemonclient_log_test.cpp
    ../../linux-update-tool/DaemonClient.cpp
    ../../linux-update-tool/models/UpdatesModel.cpp
    ../../linux-update-tool/models/LogModel.cpp
    ../../linux-update-tool/models/InstalledModel.cpp
    ../../linux-update-tool/models/HistoryModel.cpp
)
target_include_directories(daemonclient_log_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(daemonclient_log_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Test
)
add_test(NAME daemonclient_log_test COMMAND daemonclient_log_test)
