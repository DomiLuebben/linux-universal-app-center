set(CMAKE_AUTOMOC ON)

add_executable(lutd
    main.cpp
    TransactionManager.cpp
    PolicyGate.cpp
    Inhibitor.cpp
)

target_link_libraries(lutd PRIVATE
    liblut
    Qt6::Core
    Qt6::DBus
    PolkitQt6-1::Core
)

install(TARGETS lutd DESTINATION /usr/libexec)
install(FILES data/org.linuxupdatetool.Daemon1.conf DESTINATION /usr/share/dbus-1/system.d)
install(FILES data/org.linuxupdatetool.Daemon1.service DESTINATION /usr/share/dbus-1/system-services)
install(FILES data/org.linuxupdatetool.policy DESTINATION /usr/share/polkit-1/actions)
install(FILES data/lutd.service DESTINATION /usr/lib/systemd/system)
