cmake_minimum_required(VERSION 3.21)
project(zeitbenutzer CXX)

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
    src/app/typography.cpp
    src/app/theme.cpp
    src/app/settings.cpp
    src/app/settings_dialog.cpp
    src/app/external_sync.cpp
)
target_link_libraries(zb-app PUBLIC zb-storage Qt6::Widgets Qt6::Network)

# Bundled fonts (assets/fonts.qrc). Qt resources in a *static* library are not
# reliably auto-initialized, so the .qrc is compiled into each executable that
# needs them (where Qt's CMake guarantees the resource initializer is linked).

# --- the executable ----------------------------------------------------------
add_executable(zeitbenutzer src/app/main.cpp assets/fonts.qrc)
target_link_libraries(zeitbenutzer PRIVATE zb-app)

# --- smoke tests -------------------------------------------------------------
enable_testing()

add_executable(storage_smoketest tests/storage_smoketest.cpp)
target_link_libraries(storage_smoketest PRIVATE zb-storage)
add_test(NAME storage_smoketest COMMAND storage_smoketest)

add_executable(app_smoketest tests/app_smoketest.cpp assets/fonts.qrc)
target_link_libraries(app_smoketest PRIVATE zb-app)
add_test(NAME app_smoketest COMMAND app_smoketest)
set_tests_properties(app_smoketest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
