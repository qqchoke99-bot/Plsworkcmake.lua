cmake_minimum_required(VERSION 3.22)
project(SoundPhysicsLite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MOD_ID "sound_physics_lite" CACHE STRING "Levi mod id")
set(MOD_NAME "Sound Physics Lite" CACHE STRING "Mod name")
set(MOD_AUTHOR "Ported for Levi" CACHE STRING "Author")
set(MOD_VERSION "0.9.0-raycast" CACHE STRING "Version")
set(MOD_LIBRARY_NAME "SoundPhysicsLite" CACHE STRING "Library name")
set(MOD_ICON "icon.png" CACHE STRING "Icon")

# Optional: point this at a LeviLamina client SDK that exposes the public
# client headers (ll/api/service/TargetedBedrock.h + mc/world/actor/player/Player.h).
# The stock preloader-android SDK alone does not contain those headers.
set(LEVI_CLIENT_SDK_INCLUDE "" CACHE PATH "Optional Levi client SDK include directory")

include(FetchContent)
FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 0.2.2
    SOURCE_SUBDIR cmake/preloader-sdk
)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(preloader_android nlohmann_json)
FetchContent_GetProperties(preloader_android)
set(LEVI_PRELOADER_INCLUDE_DIR "${preloader_android_SOURCE_DIR}/include")

if(NOT ANDROID)
    message(FATAL_ERROR "SoundPhysicsLite is an Android/LeviLauncher mod. Configure with the Android NDK toolchain.")
endif()

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported for the target LeviLauncher build.")
endif()

if(NOT TARGET preloader)
    add_subdirectory("${preloader_android_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_preloader_android")
endif()

add_library(${MOD_LIBRARY_NAME} SHARED
    src/SoundPhysicsLite.cpp
)

set_target_properties(${MOD_LIBRARY_NAME} PROPERTIES OUTPUT_NAME "${MOD_LIBRARY_NAME}")

target_include_directories(${MOD_LIBRARY_NAME} PRIVATE
    src
    "${LEVI_PRELOADER_INCLUDE_DIR}"
)
if(LEVI_CLIENT_SDK_INCLUDE)
    target_include_directories(${MOD_LIBRARY_NAME} PRIVATE "${LEVI_CLIENT_SDK_INCLUDE}")
endif()

target_compile_options(${MOD_LIBRARY_NAME} PRIVATE
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)

target_link_options(${MOD_LIBRARY_NAME} PRIVATE "-Wl,-z,max-page-size=16384")

find_library(log-lib log REQUIRED)
target_link_libraries(${MOD_LIBRARY_NAME} PRIVATE
    preloader
    nlohmann_json::nlohmann_json
    ${log-lib}
    dl
    android
)

if(NOT CMAKE_STRIP)
    message(FATAL_ERROR "CMAKE_STRIP is required")
endif()
add_custom_command(TARGET ${MOD_LIBRARY_NAME} POST_BUILD
    COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:${MOD_LIBRARY_NAME}>"
    COMMENT "Stripping $<TARGET_FILE_NAME:${MOD_LIBRARY_NAME}>"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/manifest.json" "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" COPYONLY)

set(PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/package")
set(PACKAGE_DIR "${PACKAGE_ROOT}/SPL.levipack")
set(PACKAGE_FILE "${CMAKE_CURRENT_BINARY_DIR}/SPL.levipack")

add_custom_target(levi_package
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${MOD_LIBRARY_NAME}>" "${PACKAGE_DIR}/libSoundPhysicsLite.so"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" "${PACKAGE_DIR}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/icon.png" "${PACKAGE_DIR}/icon.png"
    COMMAND ${CMAKE_COMMAND} -E chdir "${PACKAGE_ROOT}" ${CMAKE_COMMAND} -E tar "cf" "${PACKAGE_FILE}" --format=zip "SPL.levipack"
    DEPENDS ${MOD_LIBRARY_NAME}
    COMMENT "Packaging SPL.levipack"
)
