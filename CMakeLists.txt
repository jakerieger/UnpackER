cmake_minimum_required(VERSION 3.28)
project(UnpackER)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR "UnpackER is a Windows application.")
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)

FetchContent_Declare(
        libSouls
        GIT_REPOSITORY https://github.com/jakerieger/libSouls.git
        GIT_TAG 6ccd8dc6dc1634a493d26e14d4aa7fa89f7cb6c6
)
FetchContent_MakeAvailable(libSouls)

# Point FETCHCONTENT_SOURCE_DIR_WINAPP at a local checkout to build against uncommitted WinApp changes.
FetchContent_Declare(
        WinApp
        GIT_REPOSITORY https://github.com/jakerieger/WinApp.git
        GIT_TAG 1a08dc37aeaac52103263891f75d72d9239f0a19
        EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(WinApp)

add_executable(UnpackER WIN32
        Resource/Resource.h
        Resource/App.rc
        Source/main.cpp
        Source/Extractor.cpp
        Source/Extractor.hpp
        Resource/Version.h
)

set_source_files_properties(${CMAKE_SOURCE_DIR}/Resource/App.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/Resource/App.manifest)

target_link_libraries(UnpackER PRIVATE
        Souls
        WinApp
)

target_link_options(UnpackER PRIVATE /MANIFEST:NO)

target_include_directories(UnpackER PRIVATE
        ${CMAKE_SOURCE_DIR}/Resource
)

# WinApp publicly defines UNICODE, WIN32_LEAN_AND_MEAN, NOMINMAX and the Windows 10 target version.
target_compile_definitions(UnpackER PRIVATE
        _CRT_SECURE_NO_WARNINGS
)

add_custom_command(
        TARGET UnpackER
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Vendor/BinderKeys/EldenRing_PC" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Data"
        COMMENT "Copying keys and hashes to output..."
        VERBATIM
)
add_custom_command(
        TARGET UnpackER
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/Vendor/BinderKeys/LICENSE" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Data/LICENSE"
)