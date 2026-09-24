set(CMAKE_AUTOMOC ON)

qt_policy(SET QTP0004 NEW)

qt_add_executable(linux-app-store
    media/MediaCache.cpp
    main.cpp
    AppSettings.cpp
    DaemonClient.cpp
    AurUpdates.cpp
    models/UpdatesModel.cpp
    models/LogModel.cpp
    models/InstalledModel.cpp
    models/HistoryModel.cpp
    models/TransactionPlanModel.cpp
    models/RepositoriesModel.cpp
    models/StoreModel.cpp
    models/FlatpakUpdates.cpp
    models/SnapUpdates.cpp
    models/PacstallUpdates.cpp
    catalog/CatalogService.cpp
    catalog/ApplicationStore.cpp
    theme/SystemPalette.cpp
    theme/Contrast.cpp
    AppLauncher.cpp
    ExternalChangeWatcher.cpp
    TrayManager.cpp
    TrayManager.h
    OperationMonitor.cpp
    OperationMonitor.h
    OperationResult.h
)

# BASE ist nötig: ohne landen die Dateien unter dem Alias "../data/…" und der
# Code findet sie unter :/LinuxAppStore/store/curated.json nicht. Entdecken blieb
# im installierten Paket leer (aus dem Projektordner fand der relative Fallback
# data/store/curated.json die Datei, deshalb fiel es dort nie auf).
qt_add_resources(linux-app-store "store_resources"
    PREFIX "/LinuxAppStore/store"
    BASE ../data/store
    FILES ../data/store/curated.json
)

qt_add_resources(linux-app-store "store_icons"
    PREFIX "/LinuxAppStore/icons"
    BASE ../data/icons/hicolor/scalable/apps
    FILES ../data/icons/hicolor/scalable/apps/org.linuxuniversalappcenter.svg
)

qt_add_qml_module(linux-app-store
    URI LinuxAppStore
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES
        qml/Main.qml
        qml/components/PrimaryButton.qml
        qml/components/Chip.qml
        qml/components/Card.qml
        qml/components/StatCard.qml
        qml/components/PackageRow.qml
        qml/components/SegmentedProgress.qml
        qml/components/PhaseStepper.qml
        qml/components/TaskList.qml
        qml/components/LogPane.qml
        qml/components/QuestionCard.qml
        qml/components/EmptyState.qml
        qml/components/Sparkline.qml
        qml/components/NavRail.qml
        qml/components/AppCard.qml
        qml/components/OperationProgressView.qml
        qml/components/AppTile.qml
        qml/components/ThirdPartyRiskDialog.qml
        qml/pages/Discover.qml
        qml/pages/Search.qml
        qml/pages/AppDetails.qml
        qml/pages/Updates.qml
        qml/pages/Manage.qml
        qml/pages/PlanPreview.qml
        qml/pages/Report.qml
        qml/pages/Installed.qml
        qml/pages/History.qml
        qml/pages/Logs.qml
        qml/pages/Settings.qml
)

target_include_directories(linux-app-store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(linux-app-store PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Qml
    Qt6::DBus
    Qt6::Network
    Qt6::Svg
    AppStreamQt
    KF6::Service
    KF6::KIOGui
)

# Programmname folgt dem App-Namen; das CMake-Ziel bleibt intern linux-app-store.
set_target_properties(linux-app-store PROPERTIES OUTPUT_NAME linux-universal-app-center)
install(TARGETS linux-app-store DESTINATION /usr/bin)
install(FILES ../data/org.linuxuniversalappcenter.desktop DESTINATION /usr/share/applications)
install(FILES ../data/org.linuxuniversalappcenter.metainfo.xml DESTINATION /usr/share/metainfo)
install(FILES ../data/icons/hicolor/scalable/apps/org.linuxuniversalappcenter.svg DESTINATION /usr/share/icons/hicolor/scalable/apps)
