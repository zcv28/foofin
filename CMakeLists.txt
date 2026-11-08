cmake_minimum_required(VERSION 3.21)

project(
    foofin
    VERSION 0.2.1
    DESCRIPTION "Jellyfin plugin for fooyin"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Fooyin REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

create_fooyin_plugin(
    foofin
    DEPENDS Fooyin::Core
            Fooyin::Gui
            Fooyin::Utils
            Qt6::Network
    SOURCES src/foofinplugin.cpp
            src/foofinplugin.h
            src/jellyfinarchive.cpp
            src/jellyfinarchive.h
            src/jellyfinbrowsermodel.cpp
            src/jellyfinbrowsermodel.h
            src/jellyfinbrowserwidget.cpp
            src/jellyfinbrowserwidget.h
            src/jellyfinclient.cpp
            src/jellyfinclient.h
            src/jellyfinimagecache.cpp
            src/jellyfinimagecache.h
            src/jellyfinitem.cpp
            src/jellyfinitem.h
            src/jellyfinplaybackreporter.cpp
            src/jellyfinplaybackreporter.h
            src/jellyfinplaylistsync.cpp
            src/jellyfinplaylistsync.h
            src/jellyfinsession.cpp
            src/jellyfinsession.h
            src/jellyfinsettings.cpp
            src/jellyfinsettings.h
            src/jellyfinsettingspage.cpp
            src/jellyfinsettingspage.h
            src/jellyfinsourceprovider.cpp
            src/jellyfinsourceprovider.h
            src/jellyfintrack.cpp
            src/jellyfintrack.h
)

# Installable package for fooyin's Settings → Plugins → Install:
#   cmake --build build --target package_plugin  ->  build/foofin-<os>-<arch>.fyplugin
if(COMMAND create_fooyin_plugin_bundle)
    create_fooyin_plugin_bundle(foofin FILES README.md LICENSE_FILES COPYING)
endif()

target_include_directories(foofin PRIVATE src)
target_compile_definitions(foofin PRIVATE FOOFIN_VERSION="${PROJECT_VERSION}")

option(FOOFIN_BUILD_TESTS "Build foofin tests" OFF)
if(FOOFIN_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    # The parsing/URL helpers are plain functions, so the test links them directly.
    add_executable(
        foofin_track_test
        tests/jellyfintrack_test.cpp
        src/jellyfinitem.cpp
        src/jellyfintrack.cpp
    )
    set_target_properties(foofin_track_test PROPERTIES AUTOMOC ON)
    target_include_directories(foofin_track_test PRIVATE src)
    target_link_libraries(foofin_track_test PRIVATE Fooyin::Core Qt6::Test)
    add_test(NAME foofin_track COMMAND foofin_track_test)

    # Talks to a real server; skips itself unless FOOFIN_TEST_SERVER/FOOFIN_TEST_USER are set.
    add_executable(
        foofin_live_test
        tests/jellyfin_live_test.cpp
        src/jellyfinarchive.cpp
        src/jellyfinbrowsermodel.cpp
        src/jellyfinclient.cpp
        src/jellyfinimagecache.cpp
        src/jellyfinitem.cpp
        src/jellyfinsession.cpp
        src/jellyfinsettings.cpp
        src/jellyfinsourceprovider.cpp
        src/jellyfintrack.cpp
    )
    set_target_properties(foofin_live_test PROPERTIES AUTOMOC ON)
    target_include_directories(foofin_live_test PRIVATE src)
    target_compile_definitions(foofin_live_test PRIVATE FOOFIN_VERSION="${PROJECT_VERSION}")
    target_link_libraries(foofin_live_test PRIVATE Fooyin::Core Fooyin::Gui Qt6::Network Qt6::Test)
    add_test(NAME foofin_live COMMAND foofin_live_test)
    set_tests_properties(foofin_live PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
