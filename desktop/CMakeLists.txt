# The desktop front end. Layout only -- every behaviour it triggers lives in
# vsp_core, which the Android package links against unchanged.

qt_add_executable(vitasync
    main.cpp
    # Windows only: embeds the app icon in the .exe itself. MinGW compiles it
    # with windres; every other platform ignores the file entirely.
    $<$<BOOL:${WIN32}>:vitasync.rc>
)

# Must be marked before the module is declared, otherwise the qmldir entry is
# generated as a plain type and every Theme.* binding silently resolves to
# undefined.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Stable resource names for the branding assets. Without an alias their path
# inside the module mirrors the relative source path ("../assets/...") and
# resolves to nothing at runtime.
set_source_files_properties(../assets/logo/appicon_256.png
    PROPERTIES QT_RESOURCE_ALIAS brand/appicon.png)
# The rail draws its mark at ~30px. The full shield averages into a smear at
# that size, so the rail gets the knot medallion cut instead -- the same
# split the .ico uses between its small and large frames.
set_source_files_properties(../assets/logo/appicon_32.png
    PROPERTIES QT_RESOURCE_ALIAS brand/mark.png)

# This module keeps its .qml files in components/ and views/ rather than one
# flat directory. Qt 6.9 added a policy for exactly that shape: NEW generates
# the per-directory qmldir entries itself, which is what these subfolders have
# always needed. Left unset it is only a developer warning, but the generated
# module is then the older flat-directory form.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_qml_module(vitasync
    URI VitaSync.Ui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/components/Icon.qml
        qml/components/AppButton.qml
        qml/components/InputField.qml
        qml/components/SectionLabel.qml
        qml/components/StatusDot.qml
        qml/components/StatusRail.qml
        qml/components/MeterBar.qml
        qml/components/Chip.qml
        qml/components/ClippedList.qml
        qml/components/NavRail.qml
        qml/components/TopBar.qml
        qml/components/FaqPanel.qml
        qml/components/DetailsDrawer.qml
        qml/components/ToastStrip.qml
        qml/components/CoverArt.qml
        qml/components/EmptyState.qml
        qml/components/ConfirmSheet.qml
        qml/views/ConnectView.qml
        qml/views/InstallView.qml
        qml/views/BrowseView.qml
        qml/views/TransfersView.qml
    RESOURCES
        ../assets/logo/appicon_256.png
        ../assets/logo/appicon_32.png
)


target_link_libraries(vitasync PRIVATE vsp_core vsp_media Qt6::Quick Qt6::QuickControls2)

set_target_properties(vitasync PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
)

if(ANDROID)
    # The Android package is the same binary and the same QML, wrapped. There
    # is no Java or Kotlin business logic: the shim Qt generates only starts
    # the native activity, and everything below it is vsp_core.
    set_property(TARGET vitasync PROPERTY
        QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/android)
    set_property(TARGET vitasync PROPERTY
        QT_ANDROID_PACKAGE_NAME "com.vitasync.app")
    set_property(TARGET vitasync PROPERTY
        QT_ANDROID_APP_NAME "VitaSync")
    set_property(TARGET vitasync PROPERTY
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION})
    set_property(TARGET vitasync PROPERTY QT_ANDROID_VERSION_CODE 1)
    set_property(TARGET vitasync PROPERTY QT_ANDROID_MIN_SDK_VERSION 26)
    set_property(TARGET vitasync PROPERTY QT_ANDROID_TARGET_SDK_VERSION 34)

    # The offline title index has to travel inside the APK, since there is no
    # "next to the binary" on Android.
    qt_add_resources(vitasync "vsp_assets"
        PREFIX "/assets/covers"
        BASE ${CMAKE_SOURCE_DIR}/assets/covers
        FILES ${CMAKE_SOURCE_DIR}/assets/covers/titles.json
    )
endif()

# The offline metadata index sits next to the binary so a portable copy of the
# build directory is a complete, working application.
add_custom_command(TARGET vitasync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:vitasync>/assets/covers
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/assets/covers/titles.json
            $<TARGET_FILE_DIR:vitasync>/assets/covers/titles.json
    COMMENT "Staging the offline title index"
)

install(TARGETS vitasync RUNTIME DESTINATION bin)
install(FILES ${CMAKE_SOURCE_DIR}/assets/covers/titles.json
        DESTINATION share/vitasync/assets/covers)
