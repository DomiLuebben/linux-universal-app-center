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
    target_compile_definitions(alpm_backend_test PRIVATE ALPM_WORKER_PATH="$<TARGET_FILE:lut-alpm-worker>")
    add_dependencies(alpm_backend_test lut-alpm-worker)
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

if(ALPM_FOUND)
    # AUR-Erkennung: pacman -Qm, AUR-Schnittstelle und Versionsvergleich
    add_executable(aur_updates_test
        aur_updates_test.cpp
        ../../linux-update-tool/AurUpdates.cpp
    )
    target_include_directories(aur_updates_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(aur_updates_test PRIVATE
        liblut
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME aur_updates_test COMMAND aur_updates_test)
endif()

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
    ../../linux-update-tool/models/TransactionPlanModel.cpp
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

add_executable(store_contracts_test
    store_contracts_test.cpp
    ../../linux-update-tool/DaemonClient.cpp
    ../../linux-update-tool/models/UpdatesModel.cpp
    ../../linux-update-tool/models/LogModel.cpp
    ../../linux-update-tool/models/InstalledModel.cpp
    ../../linux-update-tool/models/HistoryModel.cpp
    ../../linux-update-tool/models/TransactionPlanModel.cpp
)
target_compile_definitions(store_contracts_test PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(store_contracts_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_contracts_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Test
)
add_test(NAME store_contracts_test COMMAND store_contracts_test)

if(ALPM_FOUND)
    add_executable(store_catalog_test
        store_catalog_test.cpp
        ../../linux-update-tool/catalog/CatalogService.cpp
        ../../linux-update-tool/catalog/ApplicationStore.cpp
        ../../linux-update-tool/AppLauncher.cpp
    )
    target_compile_definitions(store_catalog_test PRIVATE
        PROJECT_DIR="${CMAKE_SOURCE_DIR}"
        STORE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/store"
    )
    target_include_directories(store_catalog_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(store_catalog_test PRIVATE
        liblut
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        AppStreamQt
        KF6::Service
        KF6::KIOGui
    )
    add_test(NAME store_catalog_test COMMAND store_catalog_test)

    add_executable(store_ui_test
        store_ui_test.cpp
        ../../linux-update-tool/catalog/CatalogService.cpp
        ../../linux-update-tool/catalog/ApplicationStore.cpp
        ../../linux-update-tool/models/StoreModel.cpp
        ../../linux-update-tool/AppLauncher.cpp
    )
    target_compile_definitions(store_ui_test PRIVATE
        PROJECT_DIR="${CMAKE_SOURCE_DIR}"
        STORE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/store"
    )
    target_include_directories(store_ui_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(store_ui_test PRIVATE
        liblut
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        AppStreamQt
        KF6::Service
        KF6::KIOGui
    )
    add_test(NAME store_ui_test COMMAND store_ui_test)
endif()

add_executable(store_installed_test
    store_installed_test.cpp
    ../../linux-update-tool/catalog/CatalogService.cpp
    ../../linux-update-tool/catalog/ApplicationStore.cpp
    ../../linux-update-tool/models/StoreModel.cpp
    ../../linux-update-tool/models/InstalledModel.cpp
    ../../linux-update-tool/AppLauncher.cpp
    ../../linux-update-tool/ExternalChangeWatcher.cpp
)
target_compile_definitions(store_installed_test PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
    STORE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/store"
)
target_include_directories(store_installed_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_installed_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    AppStreamQt
    KF6::Service
    KF6::KIOGui
)
add_test(NAME store_installed_test COMMAND store_installed_test)

add_executable(store_dbus_test
    store_dbus_test.cpp
    ../../lutd/TransactionManager.cpp
    ../../lutd/PolicyGate.cpp
    ../../lutd/Inhibitor.cpp
    ../../linux-update-tool/DaemonClient.cpp
    ../../linux-update-tool/models/UpdatesModel.cpp
    ../../linux-update-tool/models/LogModel.cpp
    ../../linux-update-tool/models/InstalledModel.cpp
    ../../linux-update-tool/models/HistoryModel.cpp
    ../../linux-update-tool/models/TransactionPlanModel.cpp
)
target_compile_definitions(store_dbus_test PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
    STORE_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/store"
)
target_include_directories(store_dbus_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_dbus_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Test
    PolkitQt6-1::Core
)
add_test(NAME store_dbus_test COMMAND store_dbus_test)

add_executable(store_dnf5_catalog_test
    store_dnf5_catalog_test.cpp
)
target_include_directories(store_dnf5_catalog_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_dnf5_catalog_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Test
)
add_test(NAME store_dnf5_catalog_test COMMAND store_dnf5_catalog_test)

add_executable(store_apt_catalog_test
    store_apt_catalog_test.cpp
)
target_include_directories(store_apt_catalog_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_apt_catalog_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Test
)
add_test(NAME store_apt_catalog_test COMMAND store_apt_catalog_test)

