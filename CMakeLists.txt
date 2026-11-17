cmake_minimum_required(VERSION 3.16)
set(PROJECT_NAME "Torchki_and_Zombie")
project(${PROJECT_NAME} VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

#======================= INCLUSION OF Qt =======================#
set(CMAKE_PREFIX_PATH $ENV{QTDIR})

find_package(Qt6 REQUIRED COMPONENTS Quick)

#=================== INCLUSION OF Project Files ====================#
set(SOURCE_DIR "${CMAKE_SOURCE_DIR}/src")

file(GLOB_RECURSE PROJECT_SOURCES
    "${SOURCE_DIR}/*.qrc"
    "${SOURCE_DIR}/*.rc"
    "${SOURCE_DIR}/*.cpp"
    "${SOURCE_DIR}/*.h"
    "${SOURCE_DIR}/*.qml"
)

file(GLOB_RECURSE QML_SOURCES RELATIVE "${CMAKE_SOURCE_DIR}"
    "${SOURCE_DIR}/*.qml"
)

#=================== SETUP EXECUTABLE ====================#
qt_standard_project_setup(REQUIRES 6.10)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

qt_add_qml_module(${PROJECT_NAME}
    URI Torchki_and_Zombie
    VERSION 1.0
    RESOURCE_PREFIX /
    QML_FILES ${QML_SOURCES}
    RESOURCES
        levels/Level1.png
)

target_include_directories(${PROJECT_NAME} PRIVATE ${SOURCE_DIR})

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
)

#===================== LINKING LIBRARIES =======================#
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Quick)

install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
