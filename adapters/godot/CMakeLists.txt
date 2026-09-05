include(FetchContent)

# Official godot-4.5-stable. The core build remains offline and engine-independent.
# Offline builds can set FETCHCONTENT_SOURCE_DIR_GODOT_CPP to an extracted checkout.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
set(GODOTCPP_BUILD_PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/build_profile.json" CACHE FILEPATH "Godot bindings profile")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GODOTCPP_BUILD_PROFILE}")
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "WorldSim handles exceptions at the engine boundary")
FetchContent_Declare(godot_cpp
    URL https://codeload.github.com/godotengine/godot-cpp/tar.gz/e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77
    URL_HASH SHA256=d4c03daf46c8bef4544614182b0762c3e32789151c7f8f44e8eb2053d5cddeba)
FetchContent_MakeAvailable(godot_cpp)
# Upstream binding headers are external code; keep strict warnings on our adapter.
set_property(TARGET godot-cpp PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    "$<TARGET_PROPERTY:godot-cpp,INTERFACE_INCLUDE_DIRECTORIES>")

add_library(worldsim_godot SHARED
    src/world_sim_bridge.cpp
    src/register_types.cpp)
target_link_libraries(worldsim_godot PRIVATE worldsim::worldsim godot::cpp)
target_compile_features(worldsim_godot PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(worldsim_godot PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(worldsim_godot PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" WORLDSIM_GODOT_PLATFORM)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" WORLDSIM_GODOT_ARCH)
if(WORLDSIM_GODOT_ARCH MATCHES "^(amd64|x86_64)$")
    set(WORLDSIM_GODOT_ARCH x86_64)
elseif(WORLDSIM_GODOT_ARCH MATCHES "^(aarch64|arm64)$")
    set(WORLDSIM_GODOT_ARCH arm64)
endif()
if(APPLE)
    set(WORLDSIM_GODOT_PLATFORM macos)
    if(CMAKE_OSX_ARCHITECTURES MATCHES ";")
        set(WORLDSIM_GODOT_ARCH universal)
    elseif(CMAKE_OSX_ARCHITECTURES)
        set(WORLDSIM_GODOT_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    endif()
endif()
set(WORLDSIM_GODOT_BIN "${PROJECT_SOURCE_DIR}/godot/bin")
set(WORLDSIM_GODOT_LIBRARY_NAME "worldsim_godot.${WORLDSIM_GODOT_PLATFORM}.${WORLDSIM_GODOT_ARCH}")
if(APPLE)
    # Godot reports the universal feature on single-architecture macOS as well.
    # One canonical filename avoids ambiguous descriptor feature matches.
    set(WORLDSIM_GODOT_LIBRARY_NAME "worldsim_godot.macos")
endif()
set_target_properties(worldsim_godot PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${WORLDSIM_GODOT_LIBRARY_NAME}"
    LIBRARY_OUTPUT_DIRECTORY "${WORLDSIM_GODOT_BIN}"
    RUNTIME_OUTPUT_DIRECTORY "${WORLDSIM_GODOT_BIN}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(worldsim_godot PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${config} "${WORLDSIM_GODOT_BIN}"
        RUNTIME_OUTPUT_DIRECTORY_${config} "${WORLDSIM_GODOT_BIN}")
endforeach()
if(BUILD_SHARED_LIBS)
    add_custom_command(TARGET worldsim_godot POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:worldsim> "${WORLDSIM_GODOT_BIN}/$<TARGET_FILE_NAME:worldsim>")
    if(APPLE)
        set_target_properties(worldsim_godot PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(worldsim_godot PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    endif()
endif()

find_program(WORLDSIM_GODOT_EXECUTABLE NAMES godot godot4)
if(WORLDSIM_BUILD_TESTS AND WORLDSIM_GODOT_EXECUTABLE)
    enable_testing()
    add_executable(worldsim_observer_fixture "${PROJECT_SOURCE_DIR}/tests/create_observer_fixture.cpp")
    target_link_libraries(worldsim_observer_fixture PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_observer_fixture
        COMMAND worldsim_observer_fixture "${CMAKE_CURRENT_BINARY_DIR}/observer_fixture.wsc")
    set_tests_properties(worldsim_observer_fixture PROPERTIES FIXTURES_SETUP observer_fixture)
    add_test(NAME worldsim_godot_import
        COMMAND "${WORLDSIM_GODOT_EXECUTABLE}" --headless --path "${PROJECT_SOURCE_DIR}/godot" --editor --import)
    set_tests_properties(worldsim_godot_import PROPERTIES
        FIXTURES_SETUP godot_import TIMEOUT 90
        FAIL_REGULAR_EXPRESSION "SCRIPT ERROR;Parse Error;Failed to load script;Error loading extension")
    add_test(NAME worldsim_godot_bridge
        COMMAND "${WORLDSIM_GODOT_EXECUTABLE}" --headless --path "${PROJECT_SOURCE_DIR}/godot"
        --script "${PROJECT_SOURCE_DIR}/tests/godot/test_bridge.gd" --
        "--fixture=${CMAKE_CURRENT_BINARY_DIR}/observer_fixture.wsc"
        "--work-dir=${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(worldsim_godot_bridge PROPERTIES
        FIXTURES_REQUIRED "godot_import;observer_fixture" TIMEOUT 90
        FAIL_REGULAR_EXPRESSION "SCRIPT ERROR;Parse Error;ERROR:")
    add_test(NAME worldsim_godot_scene
        COMMAND "${WORLDSIM_GODOT_EXECUTABLE}" --headless --path "${PROJECT_SOURCE_DIR}/godot"
        --script "${PROJECT_SOURCE_DIR}/tests/godot/test_observer.gd" --
        "--fixture=${CMAKE_CURRENT_BINARY_DIR}/observer_fixture.wsc")
    set_tests_properties(worldsim_godot_scene PROPERTIES
        FIXTURES_REQUIRED "godot_import;observer_fixture" TIMEOUT 90
        FAIL_REGULAR_EXPRESSION "SCRIPT ERROR;Parse Error;ERROR:")
    if(UNIX AND NOT APPLE)
        # Tests keep editor settings, logs and quick saves out of the developer's profile.
        set_tests_properties(worldsim_godot_import worldsim_godot_bridge worldsim_godot_scene
            PROPERTIES ENVIRONMENT
            "XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-userdata;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-cache;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config")
    endif()
elseif(WORLDSIM_BUILD_TESTS)
    message(WARNING "Godot executable not found: native build enabled, engine integration tests unavailable")
endif()
