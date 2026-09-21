set(CMAKE_AUTOMOC ON)

qt_policy(SET QTP0004 NEW)

qt_add_executable(linux-update-tool
    main.cpp
)

qt_add_qml_module(linux-update-tool
    URI LinuxUpdateTool
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES
        qml/Main.qml
)

target_link_libraries(linux-update-tool PRIVATE
    liblut
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::DBus
    Qt6::Svg
)

install(TARGETS linux-update-tool DESTINATION /usr/bin)
