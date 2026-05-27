cmake_minimum_required(VERSION 3.21)
project(zeitbenutzer VERSION 0.1.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# zeitbenutzer — time-driven planning + notes. Built incrementally per module.
#
# Each module is a static library; the GUI app module links them into the
# zeitbenutzer executable. Modules:
#   storage (0) — schema, stores, recurrence. Headless. SQLite is truth.
#   app     (1) — project tree | notes list | markdown editor (Qt Widgets).
# Later: tasks (2), calendar+recurrence (3), reconcile (4), rollups (5).
# Per-module smoke tests are built alongside.
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compile .qrc (bundled fonts) automatically

find_package(Qt6 REQUIRED COMPONENTS Core Sql Widgets Network)

# --- storage: schema, migrations, entity stores, recurrence expansion --------
add_library(zb-storage STATIC
    src/storage/database.cpp
    src/storage/recurrence.cpp
    src/storage/ical.cpp
    src/storage/store.cpp
)
target_include_directories(zb-storage PUBLIC src)
target_link_libraries(zb-storage PUBLIC Qt6::Core Qt6::Sql)

# --- app: GUI shell + module-1 panels (projects + notes) ---------------------
add_library(zb-app STATIC
    src/app/main_window.cpp
    src/app/project_tree_panel.cpp
    src/app/note_list_panel.cpp
    src/app/task_list_panel.cpp
    src/app/time_rollup_panel.cpp
    src/app/calendar_view.cpp
    src/app/block_dialog.cpp
    src/app/block_edit_dialog.cpp
    src/app/reconcile_panel.cpp
    src/app/day_review_dialog.cpp
    src/app/note_editor.cpp
    src/app/markdown_renderer.cpp
    src/app/code_highlight.cpp
    src/app/typography.cpp
    src/app/theme.cpp
    src/app/settings.cpp
    src/app/settings_dialog.cpp
    src/app/external_sync.cpp
    src/app/adopt_dialog.cpp
    src/app/calendar_sources_dialog.cpp
)
target_link_libraries(zb-app PUBLIC zb-storage Qt6::Widgets Qt6::Network)

# Bundled fonts (assets/fonts.qrc). Qt resources in a *static* library are not
# reliably auto-initialized, so the .qrc is compiled into each executable that
# needs them (where Qt's CMake guarantees the resource initializer is linked).

# --- the executable ----------------------------------------------------------
# On macOS the icon is bundled into the .app as a Resource; the MACOSX_BUNDLE
# property (set below) turns the target into a proper .app.
set(ZB_MACOS_ICON ${CMAKE_SOURCE_DIR}/packaging/icons/zeitbenutzer.icns)
add_executable(zeitbenutzer
    src/app/main.cpp
    assets/fonts.qrc
    $<$<PLATFORM_ID:Darwin>:${ZB_MACOS_ICON}>
)
target_link_libraries(zeitbenutzer PRIVATE zb-app)

if(APPLE)
    set_target_properties(zeitbenutzer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME zeitbenutzer
        MACOSX_BUNDLE_GUI_IDENTIFIER com.eavalenzuela.zeitbenutzer
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE zeitbenutzer.icns
        RESOURCE ${ZB_MACOS_ICON}
    )
endif()

# --- smoke tests -------------------------------------------------------------
enable_testing()

add_executable(storage_smoketest tests/storage_smoketest.cpp)
target_link_libraries(storage_smoketest PRIVATE zb-storage)
add_test(NAME storage_smoketest COMMAND storage_smoketest)

add_executable(app_smoketest tests/app_smoketest.cpp assets/fonts.qrc)
target_link_libraries(app_smoketest PRIVATE zb-app)
add_test(NAME app_smoketest COMMAND app_smoketest)
set_tests_properties(app_smoketest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- install rules -----------------------------------------------------------
# Drive both the Linux .deb and the macOS .app/.pkg from one set of rules.
# On macOS the bundle goes to /Applications; on Linux the bare ELF + the
# freedesktop integration (icons, .desktop) land under the usual prefixes.
if(APPLE)
    install(TARGETS zeitbenutzer BUNDLE DESTINATION .)
else()
    include(GNUInstallDirs)
    install(TARGETS zeitbenutzer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES packaging/zeitbenutzer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    foreach(sz 16 32 48 64 128 256 512)
        install(FILES packaging/icons/hicolor/${sz}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps
                RENAME zeitbenutzer.png)
    endforeach()

    install(FILES README.md DESIGN.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

# --- packaging (CPack) -------------------------------------------------------
set(CPACK_PACKAGE_NAME zeitbenutzer)
set(CPACK_PACKAGE_VENDOR "E. A. Valenzuela")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Time-driven planning + notes: plan in blocks, reconcile, see time per project")
set(CPACK_PACKAGE_CONTACT "eavalenzuela7@gmail.com")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/eavalenzuela/zeitbenutzer")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_STRIP_FILES ON)

if(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/Applications")
else()
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "E. A. Valenzuela <eavalenzuela7@gmail.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
    # Resolve most Qt deps automatically from the linked sym tables...
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # ...but the SQLite driver is a runtime-loaded plugin, not a link-time dep,
    # so shlibdeps can't see it. Add it (and qpa plugins) explicitly.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite")
endif()

include(CPack)
