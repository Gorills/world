cmake_minimum_required(VERSION 3.20)
project(worldsim VERSION 0.6.0 LANGUAGES C CXX)

option(WORLDSIM_BUILD_TESTS "Build WorldSim tests" ON)

add_library(worldsim
    src/world.cpp
    src/hydrology.cpp
    src/continental_hydrology.cpp
    src/dynamic_hydrology.cpp
    src/continental_water.cpp
    src/multiresolution_water.cpp
    src/multiresolution_water_persistence.cpp
    src/persistence.cpp
    src/c_api.cpp
    src/multiresolution_water_c_api.cpp
)
add_library(worldsim::worldsim ALIAS worldsim)

target_compile_features(worldsim PUBLIC cxx_std_20)
target_include_directories(worldsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(worldsim PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(worldsim PRIVATE WORLDSIM_BUILD_SHARED)
    target_compile_definitions(worldsim INTERFACE WORLDSIM_USE_SHARED)
endif()

if(MSVC)
    target_compile_options(worldsim PRIVATE /W4 /permissive-)
else()
    target_compile_options(worldsim PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(worldsim_cli src/main.cpp)
target_link_libraries(worldsim_cli PRIVATE worldsim::worldsim)

if(WORLDSIM_BUILD_TESTS)
    enable_testing()
    add_executable(worldsim_tests tests/test_world.cpp)
    target_link_libraries(worldsim_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_tests COMMAND worldsim_tests)

    add_executable(worldsim_continental_water_tests tests/test_continental_water.cpp)
    target_link_libraries(worldsim_continental_water_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_continental_water_tests COMMAND worldsim_continental_water_tests)

    add_executable(worldsim_multiresolution_water_tests tests/test_multiresolution_water.cpp)
    target_link_libraries(worldsim_multiresolution_water_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_multiresolution_water_tests COMMAND worldsim_multiresolution_water_tests)

    add_executable(worldsim_multiresolution_water_persistence_tests
        tests/test_multiresolution_water_persistence.cpp)
    target_link_libraries(worldsim_multiresolution_water_persistence_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_multiresolution_water_persistence_tests
        COMMAND worldsim_multiresolution_water_persistence_tests)

    add_executable(worldsim_c_api_tests tests/test_c_api.c)
    target_link_libraries(worldsim_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_c_api_tests COMMAND worldsim_c_api_tests)

    add_executable(worldsim_multiresolution_water_c_api_tests
        tests/test_multiresolution_water_c_api.c)
    target_link_libraries(worldsim_multiresolution_water_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_multiresolution_water_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_multiresolution_water_c_api_tests
        COMMAND worldsim_multiresolution_water_c_api_tests)
endif()
