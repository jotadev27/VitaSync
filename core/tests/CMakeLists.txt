find_package(ZLIB REQUIRED)

# Pure-logic tests: parsing, path hygiene, package identification.
qt_add_executable(tst_core tst_core.cpp TestArchives.h)
target_link_libraries(tst_core PRIVATE vsp_core Qt6::Test ZLIB::ZLIB)
add_test(NAME core COMMAND tst_core)

# Transport tests, run against an in-process stand-in for VitaShell's server.
qt_add_executable(tst_transport
    tst_transport.cpp
    MockVitaServer.h
    MockVitaServer.cpp
    TestArchives.h
)
target_link_libraries(tst_transport PRIVATE vsp_core Qt6::Test ZLIB::ZLIB)
add_test(NAME transport COMMAND tst_transport)

# The USB transport, driven against a plain directory carrying the same
# signature a mounted Vita card does -- see docs/usb.md for why there is no
# protocol to fake the way MockVitaServer fakes FTP.
qt_add_executable(tst_usb tst_usb.cpp)
target_link_libraries(tst_usb PRIVATE vsp_core Qt6::Test)
add_test(NAME usb COMMAND tst_usb)

# Integration tests for the object the UI binds to.
qt_add_executable(tst_device
    tst_device.cpp
    MockVitaServer.h
    MockVitaServer.cpp
    TestArchives.h
)
target_link_libraries(tst_device PRIVATE vsp_core Qt6::Test ZLIB::ZLIB)
add_test(NAME device COMMAND tst_device)

# A runnable version of the same stand-in device, so the GUI can be driven
# against something that behaves like a Vita.
qt_add_executable(mockvita
    mockvita_main.cpp
    MockVitaServer.h
    MockVitaServer.cpp
)
target_link_libraries(mockvita PRIVATE vsp_core Qt6::Network)
