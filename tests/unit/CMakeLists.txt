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
    ../../linux-app-store/theme/Contrast.cpp
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
        ../../linux-app-store/AurUpdates.cpp
        ../../linux-app-store/AppSettings.cpp
    )
    target_include_directories(aur_updates_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(aur_updates_test PRIVATE
        liblut
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME aur_updates_test COMMAND aur_updates_test)

    add_executable(app_settings_test
        app_settings_test.cpp
        ../../linux-app-store/AppSettings.cpp
        ../../linux-app-store/AurUpdates.cpp
    )
    target_include_directories(app_settings_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(app_settings_test PRIVATE
        liblut
        Qt6::Core
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME app_settings_test COMMAND app_settings_test)
endif()

# Farbschema aus kdeglobals: QSettings-Falle und Flächenableitung
add_executable(kdeglobals_test
    kdeglobals_test.cpp
    ../../linux-app-store/theme/SystemPalette.cpp
    ../../linux-app-store/theme/Contrast.cpp
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
    ../../linux-app-store/models/UpdatesModel.cpp
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
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/models/UpdatesModel.cpp
    ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp
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
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/models/UpdatesModel.cpp
    ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp
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
        ../../linux-app-store/catalog/CatalogService.cpp
        ../../linux-app-store/catalog/ApplicationStore.cpp
        ../../linux-app-store/AppLauncher.cpp
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
        ../../linux-app-store/catalog/CatalogService.cpp
        ../../linux-app-store/catalog/ApplicationStore.cpp
        ../../linux-app-store/models/StoreModel.cpp
        ../../linux-app-store/AppLauncher.cpp
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
    ../../linux-app-store/catalog/CatalogService.cpp
    ../../linux-app-store/catalog/ApplicationStore.cpp
    ../../linux-app-store/models/StoreModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/AppLauncher.cpp
    ../../linux-app-store/ExternalChangeWatcher.cpp
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
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/models/UpdatesModel.cpp
    ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp
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

add_lut_test(apt_guard_test)
add_lut_test(repo_manager_test)
add_executable(store_media_test store_media_test.cpp ../../linux-app-store/media/MediaCache.cpp)
target_include_directories(store_media_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_media_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
add_test(NAME store_media_test COMMAND store_media_test)

add_executable(store_flatpak_test
    store_flatpak_test.cpp
    ../../lutd/TransactionManager.cpp
    ../../lutd/PolicyGate.cpp
    ../../lutd/Inhibitor.cpp
)
target_include_directories(store_flatpak_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_flatpak_test PRIVATE
    liblut
    Qt6::Core
    Qt6::DBus
    Qt6::Test
    PolkitQt6-1::Core
)
add_test(NAME store_flatpak_test COMMAND store_flatpak_test)

add_executable(store_snap_test
    store_snap_test.cpp
    ../../lutd/TransactionManager.cpp
    ../../lutd/PolicyGate.cpp
    ../../lutd/Inhibitor.cpp
)
target_include_directories(store_snap_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_snap_test PRIVATE
    liblut
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    Qt6::Test
    PolkitQt6-1::Core
)
add_test(NAME store_snap_test COMMAND store_snap_test)

add_executable(store_bus_integration_test
    store_bus_integration_test.cpp
    ../../lutd/TransactionManager.cpp ../../lutd/PolicyGate.cpp ../../lutd/Inhibitor.cpp
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/models/UpdatesModel.cpp ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp)
target_include_directories(store_bus_integration_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_bus_integration_test PRIVATE liblut Qt6::Core Qt6::Gui Qt6::DBus Qt6::Test PolkitQt6-1::Core)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_test(NAME store_bus_integration_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:store_bus_integration_test>)
set_tests_properties(store_bus_integration_test PROPERTIES TIMEOUT 30)

target_sources(store_bus_integration_test PRIVATE
    ../../linux-app-store/catalog/CatalogService.cpp ../../linux-app-store/catalog/ApplicationStore.cpp
    ../../linux-app-store/AppLauncher.cpp ../../linux-app-store/theme/SystemPalette.cpp
    ../../linux-app-store/theme/Contrast.cpp ../../linux-app-store/media/MediaCache.cpp)
target_compile_definitions(store_bus_integration_test PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(store_bus_integration_test PRIVATE Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Network AppStreamQt KF6::Service KF6::KIOGui)
set_tests_properties(store_bus_integration_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

add_executable(store_updates_history_test
    store_updates_history_test.cpp
    ../../linux-app-store/models/FlatpakUpdates.cpp
    ../../linux-app-store/models/SnapUpdates.cpp
    ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/UpdatesModel.cpp
    ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/AppSettings.cpp
    ../../linux-app-store/AurUpdates.cpp
    ../../linux-app-store/TrayManager.cpp
    ../../linux-app-store/OperationMonitor.cpp
    ../../linux-app-store/models/PacstallUpdates.cpp
)
target_include_directories(store_updates_history_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(store_updates_history_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Network
    Qt6::Test
    PolkitQt6-1::Core
)
add_test(NAME store_updates_history_test COMMAND store_updates_history_test)


# Aktualisierungsablauf: Liste nach Erfolg leeren, gemeinsamer Fortschritt,
# Zahl im Tray-Symbol.
add_executable(update_progress_test
    update_progress_test.cpp
    ../../linux-app-store/models/FlatpakUpdates.cpp
    ../../linux-app-store/models/SnapUpdates.cpp
    ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/UpdatesModel.cpp
    ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/AppSettings.cpp
    ../../linux-app-store/AurUpdates.cpp
    ../../linux-app-store/TrayManager.cpp
    ../../linux-app-store/OperationMonitor.cpp
    ../../linux-app-store/models/PacstallUpdates.cpp
)
target_include_directories(update_progress_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(update_progress_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Network
    Qt6::Test
    PolkitQt6-1::Core
)
add_test(NAME update_progress_test COMMAND update_progress_test)
set_tests_properties(update_progress_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Pflichttest Abschnitt 6.3: Keine Geisterquellen-Texte bei nicht installierten Quellen
add_executable(phantom_sources_test
    phantom_sources_test.cpp
    ../../linux-app-store/models/FlatpakUpdates.cpp
    ../../linux-app-store/models/SnapUpdates.cpp
    ../../linux-app-store/models/HistoryModel.cpp
    ../../linux-app-store/models/UpdatesModel.cpp
    ../../linux-app-store/models/LogModel.cpp
    ../../linux-app-store/models/InstalledModel.cpp
    ../../linux-app-store/models/TransactionPlanModel.cpp
    ../../linux-app-store/models/RepositoriesModel.cpp
    ../../linux-app-store/models/StoreModel.cpp
    ../../linux-app-store/catalog/CatalogService.cpp
    ../../linux-app-store/catalog/ApplicationStore.cpp
    ../../linux-app-store/theme/SystemPalette.cpp
    ../../linux-app-store/theme/Contrast.cpp
    ../../linux-app-store/AppLauncher.cpp
    ../../linux-app-store/media/MediaCache.cpp
    ../../linux-app-store/ExternalChangeWatcher.cpp
    ../../linux-app-store/DaemonClient.cpp
    ../../linux-app-store/AppSettings.cpp
    ../../linux-app-store/AurUpdates.cpp
    ../../linux-app-store/TrayManager.cpp
    ../../linux-app-store/OperationMonitor.cpp
    ../../linux-app-store/models/PacstallUpdates.cpp
)
target_include_directories(phantom_sources_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/linux-app-store
)
target_compile_definitions(phantom_sources_test PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(phantom_sources_test PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    Qt6::DBus
    Qt6::Network
    Qt6::Test
    AppStreamQt
    KF6::Service
    KF6::KIOGui
    PolkitQt6-1::Core
)
add_test(NAME phantom_sources_test COMMAND phantom_sources_test)
set_tests_properties(phantom_sources_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

# Pacstall-Backend Komponententest (Abschnitt 8 D5)
add_executable(pacstall_backend_test
    pacstall_backend_test.cpp
    ../../linux-app-store/models/PacstallUpdates.cpp
    ../../linux-app-store/AppSettings.cpp
)
target_include_directories(pacstall_backend_test PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/linux-app-store
)
target_link_libraries(pacstall_backend_test PRIVATE
    liblut
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    Qt6::Test
)
add_test(NAME pacstall_backend_test COMMAND pacstall_backend_test)

