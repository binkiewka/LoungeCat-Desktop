cmake_minimum_required(VERSION 3.24)
project(LoungeCat VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(CTest)
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Sql
    Concurrent
    Svg
)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()

set(LOUNGECAT_SANITIZERS "none" CACHE STRING "none, address, or thread")
set_property(CACHE LOUNGECAT_SANITIZERS PROPERTY STRINGS none address thread)
add_library(loungecat_options INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(loungecat_options INTERFACE -Wall -Wextra -Wpedantic)
    if(LOUNGECAT_SANITIZERS STREQUAL "address")
        target_compile_options(loungecat_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(loungecat_options INTERFACE -fsanitize=address,undefined)
    elseif(LOUNGECAT_SANITIZERS STREQUAL "thread")
        target_compile_options(loungecat_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(loungecat_options INTERFACE -fsanitize=thread)
    elseif(NOT LOUNGECAT_SANITIZERS STREQUAL "none")
        message(FATAL_ERROR "Unknown LOUNGECAT_SANITIZERS value")
    endif()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ALSA QUIET IMPORTED_TARGET alsa)
endif()

add_compile_definitions(
    APP_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS
)

add_library(loungecat_core STATIC
    src/app/AppConfig.h
    src/app/AppConfig.cpp
    src/app/SingleInstanceGuard.h
    src/app/SingleInstanceGuard.cpp
    src/data/model/ServerConfig.h
    src/data/model/ServerConfig.cpp
    src/data/model/IncomingMessage.h
    src/data/model/IncomingMessage.cpp
    src/data/model/Channel.h
    src/data/model/Channel.cpp
    src/data/model/ChannelUser.h
    src/data/model/ChannelUser.cpp
    src/data/model/UserPreferences.h
    src/data/model/UserPreferences.cpp
    src/data/model/ServerConnection.h
    src/data/model/ServerConnection.cpp
    src/data/model/UrlPreview.h
    src/data/model/UrlPreview.cpp
    src/data/model/WhoisInfo.h
    src/data/model/WhoisInfo.cpp
    src/data/database/DatabaseService.h
    src/data/database/DatabaseService.cpp
    src/data/cache/MessageCache.h
    src/data/cache/MessageCache.cpp
    src/data/preferences/PreferencesManager.h
    src/data/preferences/PreferencesManager.cpp
    src/network/ChatClient.h
    src/network/ChatClient.cpp
    src/network/IrcClient.h
    src/network/IrcClient.cpp
    src/network/ConnectionManager.h
    src/network/ConnectionManager.cpp
    src/service/LinkPreviewService.h
    src/service/LinkPreviewService.cpp
    src/service/ImageCache.h
    src/service/ImageCache.cpp
    src/service/PastebinService.h
    src/service/PastebinService.cpp
    src/service/ImgBbService.h
    src/service/ImgBbService.cpp
    src/service/UrlShortenerService.h
    src/service/UrlShortenerService.cpp
    src/service/TextLogService.h
    src/service/TextLogService.cpp
    src/service/SoundService.h
    src/service/SoundService.cpp
    src/service/UpdateCheckService.h
    src/service/UpdateCheckService.cpp
    src/util/PlatformUtils.h
    src/util/PlatformUtils.cpp
    src/util/NetworkSecurityUtils.h
    src/util/NetworkSecurityUtils.cpp
    src/util/IrcCommandParser.h
    src/util/IrcCommandParser.cpp
    src/util/IrcFormatParser.h
    src/util/IrcFormatParser.cpp
    src/util/NickColorGenerator.h
    src/util/NickColorGenerator.cpp
    src/util/HighlightMatcher.h
    src/util/HighlightMatcher.cpp
    src/util/ChatGrouping.h
    src/util/ChatGrouping.cpp
    src/util/TabCompletion.h
    src/util/TabCompletion.cpp
    src/util/SpellChecker.h
    src/util/SpellChecker.cpp
    src/util/SystemInfoCollector.h
    src/util/SystemInfoCollector.cpp
    src/util/ChatExporter.h
    src/util/ChatExporter.cpp
    src/util/ConfigExporter.h
    src/util/ConfigExporter.cpp
    src/ui/theme/Theme.h
    src/ui/theme/Theme.cpp
    src/ui/theme/ThemeManager.h
    src/ui/theme/ThemeManager.cpp
    src/ui/theme/IconFactory.h
    src/ui/theme/IconFactory.cpp
    src/ui/chat/MessageListModel.h
    src/ui/chat/MessageListModel.cpp
    src/ui/chat/MessageItemDelegate.h
    src/ui/chat/MessageItemDelegate.cpp
)

target_include_directories(loungecat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(loungecat_core PUBLIC
    loungecat_options
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Svg
)

if(ALSA_FOUND)
    target_compile_definitions(loungecat_core PUBLIC HAVE_ALSA=1)
    target_link_libraries(loungecat_core PUBLIC PkgConfig::ALSA)
endif()

add_library(loungecat_ui STATIC
    src/ui/window/MainWindow.h
    src/ui/window/MainWindow.cpp
    src/ui/sidebar/ServerSidebarWidget.h
    src/ui/sidebar/ServerSidebarWidget.cpp
    src/ui/sidebar/UserListSidebarWidget.h
    src/ui/sidebar/UserListSidebarWidget.cpp
    src/ui/chat/SplitViewContainer.h
    src/ui/chat/SplitViewContainer.cpp
    src/ui/chat/ChatGridContainer.h
    src/ui/chat/ChatGridContainer.cpp
    src/ui/chat/ChannelPaneWidget.h
    src/ui/chat/ChannelPaneWidget.cpp
    src/ui/chat/MessageListView.h
    src/ui/chat/MessageListView.cpp
    src/ui/chat/ChatInputWidget.h
    src/ui/chat/ChatInputWidget.cpp
    src/ui/chat/SearchOverlayBar.h
    src/ui/chat/SearchOverlayBar.cpp
    src/ui/chat/UrlPreviewCardWidget.h
    src/ui/chat/UrlPreviewCardWidget.cpp
    src/ui/dialog/ConnectServerDialog.h
    src/ui/dialog/ConnectServerDialog.cpp
    src/ui/dialog/SettingsDialog.h
    src/ui/dialog/SettingsDialog.cpp
    src/ui/dialog/ModerationDialog.h
    src/ui/dialog/ModerationDialog.cpp
    src/ui/dialog/ChannelListDialog.h
    src/ui/dialog/ChannelListDialog.cpp
    src/ui/dialog/ThemeEditorDialog.h
    src/ui/dialog/ThemeEditorDialog.cpp
    src/ui/dialog/HelpDialog.h
    src/ui/dialog/HelpDialog.cpp
    src/ui/tray/SystemTrayController.h
    src/ui/tray/SystemTrayController.cpp
)

target_link_libraries(loungecat_ui PUBLIC loungecat_core)
add_executable(loungecat src/main.cpp resources/resources.qrc)
target_link_libraries(loungecat PRIVATE loungecat_ui)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(GNUInstallDirs)
    install(TARGETS loungecat RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/io.github.binkiewka.LoungeCat.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/icons/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME loungecat.png)
    install(FILES LICENSE COPYRIGHT THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/loungecat)
    install(FILES COPYRIGHT DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/loungecat RENAME copyright)
    install(FILES third_party/licenses/LoungeCat-Kotlin-MIT.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/loungecat/licenses)

    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGE_NAME loungecat)
    set(CPACK_PACKAGE_VENDOR "LoungeCat contributors")
    set(CPACK_PACKAGE_CONTACT "https://github.com/binkiewka/LoungeCat-Desktop/issues")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Qt desktop IRC client")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION net)
    set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LoungeCat contributors <binkiewka@users.noreply.github.com>")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # Plugins/backends loaded at runtime are not found by ELF shlibdeps.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, qt6-qpa-plugins, libssl3, ca-certificates")
    include(CPack)
endif()
