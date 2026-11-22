cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME "Torchki_and_Zombie")
project(${PROJECT_NAME} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 QuickLayouts QuickControls2Basic QuickControls2Material)
else()
    if(DEFINED ENV{QTDIR})
        set(CMAKE_PREFIX_PATH $ENV{QTDIR})
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Quick)
endif()

# Отключаем вызов qmlimportscanner на этапе CMake для обеих платформ (Android и ПК)
macro(qt6_import_qml_plugins)
endmacro()

qt_standard_project_setup(REQUIRES 6.10)

set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")

file(GLOB_RECURSE PROJECT_SOURCES
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/*.h"
)

if(WIN32)
    file(GLOB_RECURSE WIN_RESOURCES
        "${SOURCE_DIR}/*.qrc"
        "${SOURCE_DIR}/*.rc"
    )
    list(APPEND PROJECT_SOURCES ${WIN_RESOURCES})
endif()

file(GLOB_RECURSE LEVEL_ASSETS RELATIVE "${CMAKE_SOURCE_DIR}"
    "levels/*.png"
)

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

qt_add_qml_module(${PROJECT_NAME}
    URI TorchkiAndZombie
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        src/Main.qml
        src/ToolkitMenu.qml
        src/TopHud.qml
        src/LegendPanel.qml
        src/AlertsPanel.qml
        src/ToolBar.qml
        src/SelectionOverlays.qml
        src/WorldOverlays.qml
        src/NightOverlay.qml
    RESOURCES
        ${LEVEL_ASSETS}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${SOURCE_DIR})

if(ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickLayouts
        Qt6::QuickControls2Basic
        Qt6::QuickControls2Material
    )

    set(DEPLOY_JSON "${CMAKE_BINARY_DIR}/android-${PROJECT_NAME}-deployment-settings.json")
    set(FIX_SCRIPT "${CMAKE_BINARY_DIR}/fix_deploy_json.cmake")

    file(WRITE "${FIX_SCRIPT}" [=[
if(EXISTS "${JSON_FILE}")
    file(READ "${JSON_FILE}" CONTENT)
    string(REGEX REPLACE "\"qml-root-path\"[ \t]*:[ \t]*\\[[^]]*\\]" "\"qml-root-path\" : [ ]" CONTENT "${CONTENT}")
    file(WRITE "${JSON_FILE}" "${CONTENT}")
endif()

set(QT_ANDROID_DIR "A:/Qt/6.12.0/android_arm64_v8a")
set(BUILD_ANDROID_DIR "${BIN_DIR}/android-build-Torchki_and_Zombie")

file(COPY "${QT_ANDROID_DIR}/qml/QtQuick" DESTINATION "${BUILD_ANDROID_DIR}/assets/qt/qml")

file(GLOB_RECURSE QML_SO_FILES "${QT_ANDROID_DIR}/qml/QtQuick/*_arm64-v8a.so")
file(MAKE_DIRECTORY "${BUILD_ANDROID_DIR}/libs/arm64-v8a")
file(COPY ${QML_SO_FILES} DESTINATION "${BUILD_ANDROID_DIR}/libs/arm64-v8a")

file(GLOB QT_QUICK_LIBS
    "${QT_ANDROID_DIR}/lib/libQt6Quick*_arm64-v8a.so"
    "${QT_ANDROID_DIR}/lib/libQt6Qml*_arm64-v8a.so"
)
file(COPY ${QT_QUICK_LIBS} DESTINATION "${BUILD_ANDROID_DIR}/libs/arm64-v8a")
]=])

    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DJSON_FILE="${DEPLOY_JSON}" -DBIN_DIR="${CMAKE_BINARY_DIR}" -P "${FIX_SCRIPT}"
        COMMENT "Preparing QML assets and plugins for Android APK"
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Quick)

    if(WIN32)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
    endif()
endif()

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
