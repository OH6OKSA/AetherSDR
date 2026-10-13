# aether-mobile — touch client that links libaethercore in-process and talks to
# the radio directly (no PC or aetherd in between). Configured from the root
# CMakeLists.txt only when AETHER_MOBILE is ON. See mobile/README.md.

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.12)

# Core reads ":/..." assets (demo voice, band plans) from the executable's
# resources, like the desktop app. radar.qrc is map-only and stays out.
qt_add_resources(AETHER_MOBILE_RESOURCES ${CMAKE_SOURCE_DIR}/resources/resources.qrc)

qt_add_executable(aether-mobile
    src/main.cpp
    src/MobileEngine.cpp
    src/MobileEngine.h
    src/RadioListModel.cpp
    src/RadioListModel.h
    src/SpectrumItem.cpp
    src/SpectrumItem.h
    src/ScreenAwake.h
    src/LogBuffer.cpp
    src/LogBuffer.h
    ${AETHER_MOBILE_RESOURCES}
)
if(IOS)
    target_sources(aether-mobile PRIVATE ios/ScreenAwake.mm)
else()
    target_sources(aether-mobile PRIVATE src/ScreenAwake.cpp)
endif()

# Must precede qt_add_qml_module, which reads it when writing the qmldir.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(aether-mobile
    URI AetherMobile
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/ConnectPage.qml
        qml/OperatePage.qml
        qml/Theme.qml
        qml/FrequencyDialog.qml
        qml/LogPage.qml
        qml/PanadapterView.qml
        qml/AppButton.qml
        qml/ControlsPanel.qml
        qml/MeterView.qml
        qml/TxControls.qml
        qml/PhoneOperatePage.qml
)

# The generated QML type registrations include the headers by bare name.
target_include_directories(aether-mobile PRIVATE src)

target_link_libraries(aether-mobile PRIVATE
    aethercore
    Qt6::Quick
    Qt6::QuickControls2
)

if(MSVC)
    target_compile_options(aether-mobile PRIVATE /W3 /Zc:__cplusplus /permissive- /utf-8)
else()
    target_compile_options(aether-mobile PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Signing identity of the iOS app. The bundle ID must be one the signing
# Apple team owns; the build number must grow with every TestFlight upload.
set(AETHER_MOBILE_BUNDLE_ID "com.aethersdr.mobile" CACHE STRING "iOS bundle identifier of aether-mobile")
set(AETHER_MOBILE_BUILD_NUMBER "1" CACHE STRING "iOS CFBundleVersion of aether-mobile")
set(AETHER_MOBILE_TEAM_ID "" CACHE STRING "Apple Developer Team ID that signs aether-mobile (empty: unsigned)")

if(IOS)
    # Audio goes through the Darwin multimedia backend; the FFmpeg plugin
    # would need FFmpeg frameworks the app neither ships nor uses.
    qt_import_plugins(aether-mobile EXCLUDE Qt6::QFFmpegMediaPlugin)
    target_sources(aether-mobile PRIVATE ios/Assets.xcassets)
    set_source_files_properties(ios/Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(aether-mobile PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "${AETHER_MOBILE_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "AetherSDR"
        MACOSX_BUNDLE_BUNDLE_VERSION "${AETHER_MOBILE_BUILD_NUMBER}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${AETHER_MOBILE_BUNDLE_ID}"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${AETHER_MOBILE_BUILD_NUMBER}"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${PROJECT_VERSION}"
    )
    if(AETHER_MOBILE_TEAM_ID)
        set_target_properties(aether-mobile PROPERTIES
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${AETHER_MOBILE_TEAM_ID}"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
        )
    endif()
endif()
