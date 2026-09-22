set(CMAKE_AUTOMOC ON)

qt_policy(SET QTP0004 NEW)

qt_add_executable(linux-update-tool
    main.cpp
    DaemonClient.cpp
    AurUpdates.cpp
    models/UpdatesModel.cpp
    models/LogModel.cpp
    models/InstalledModel.cpp
    models/HistoryModel.cpp
    models/TransactionPlanModel.cpp
    models/StoreModel.cpp
    catalog/CatalogService.cpp
    catalog/ApplicationStore.cpp
    theme/SystemPalette.cpp
    theme/Contrast.cpp
    AppLauncher.cpp
    ExternalChangeWatcher.cpp
)

qt_add_resources(linux-update-tool "store_resources"
    PREFIX "/LinuxUpdateTool/store"
    FILES ../data/store/curated.json
)

qt_add_qml_module(linux-update-tool
    URI LinuxUpdateTool
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
        qml/pages/Discover.qml
        qml/pages/Search.qml
        qml/pages/AppDetails.qml
        qml/pages/Updates.qml
        qml/pages/Transaction.qml
        qml/pages/Report.qml
        qml/pages/Installed.qml
        qml/pages/History.qml
        qml/pages/Logs.qml
        qml/pages/Settings.qml
)

target_include_directories(linux-update-tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(linux-update-tool PRIVATE
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

install(TARGETS linux-update-tool DESTINATION /usr/bin)
install(FILES ../data/org.linuxupdatetool.desktop DESTINATION /usr/share/applications)
install(FILES ../data/org.linuxupdatetool.metainfo.xml DESTINATION /usr/share/metainfo)
install(FILES ../data/icons/hicolor/scalable/apps/org.linuxupdatetool.svg DESTINATION /usr/share/icons/hicolor/scalable/apps)
