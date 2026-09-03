cmake_minimum_required(VERSION 3.20)
project(worldsim VERSION 0.11.0 LANGUAGES C CXX)

option(WORLDSIM_BUILD_TESTS "Build WorldSim tests" ON)

add_library(worldsim
    src/world.cpp
    src/soil.cpp
    src/hydrology.cpp
    src/continental_hydrology.cpp
    src/dynamic_hydrology.cpp
    src/continental_water.cpp
    src/multiresolution_water.cpp
    src/multiresolution_water_persistence.cpp
    src/weather.cpp
    src/weather_persistence.cpp
    src/simulation.cpp
    src/simulation_persistence.cpp
    src/persistence.cpp
    src/c_api.cpp
    src/soil_c_api.cpp
    src/multiresolution_water_c_api.cpp
    src/weather_c_api.cpp
    src/simulation_c_api.cpp
)
add_library(worldsim::worldsim ALIAS worldsim)

target_compile_features(worldsim PUBLIC cxx_std_20)
target_include_directories(worldsim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(worldsim PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(worldsim PRIVATE WORLDSIM_BUILD_SHARED)
    target_compile_definitions(worldsim INTERFACE WORLDSIM_USE_SHARED)
    if(WIN32)
        # Public C++ consumers (CLI/tests and native engine adapters) also link the shared target.
        # The C ABI has explicit WORLDSIM_API annotations; export the remaining C++ symbols on
        # Windows so BUILD_SHARED_LIBS has the same usable target contract as Unix platforms.
        set_target_properties(worldsim PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
endif()

if(MSVC)
    target_compile_options(worldsim PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(worldsim PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(worldsim_cli src/main.cpp)
target_link_libraries(worldsim_cli PRIVATE worldsim::worldsim)

add_executable(worldsim_multiresolution_water_benchmark
    benchmarks/benchmark_multiresolution_water.cpp)
target_link_libraries(worldsim_multiresolution_water_benchmark PRIVATE worldsim::worldsim)

add_executable(worldsim_weather_benchmark benchmarks/benchmark_weather.cpp)
target_link_libraries(worldsim_weather_benchmark PRIVATE worldsim::worldsim)

add_executable(worldsim_simulation_benchmark benchmarks/benchmark_simulation.cpp)
target_link_libraries(worldsim_simulation_benchmark PRIVATE worldsim::worldsim)

if(WORLDSIM_BUILD_TESTS)
    enable_testing()
    add_executable(worldsim_tests tests/test_world.cpp)
    target_link_libraries(worldsim_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_tests COMMAND worldsim_tests)

    add_executable(worldsim_soil_tests tests/test_soil.cpp)
    target_link_libraries(worldsim_soil_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_soil_tests COMMAND worldsim_soil_tests)

    add_executable(worldsim_soil_water_capacity_tests tests/test_soil_water_capacity.cpp)
    target_link_libraries(worldsim_soil_water_capacity_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_soil_water_capacity_tests COMMAND worldsim_soil_water_capacity_tests)

    add_executable(worldsim_weather_tests tests/test_weather.cpp)
    target_link_libraries(worldsim_weather_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_weather_tests COMMAND worldsim_weather_tests)

    add_executable(worldsim_weather_climatology_tests tests/test_weather_climatology.cpp)
    target_link_libraries(worldsim_weather_climatology_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_weather_climatology_tests COMMAND worldsim_weather_climatology_tests)

    add_executable(worldsim_weather_climatology_matrix_tests
        tests/test_weather_climatology_matrix.cpp)
    target_link_libraries(worldsim_weather_climatology_matrix_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_weather_climatology_matrix_tests
        COMMAND worldsim_weather_climatology_matrix_tests)

    add_executable(worldsim_weather_atomicity_tests tests/test_weather_atomicity.cpp)
    target_link_libraries(worldsim_weather_atomicity_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_weather_atomicity_tests COMMAND worldsim_weather_atomicity_tests)

    add_executable(worldsim_simulation_tests tests/test_simulation.cpp)
    target_link_libraries(worldsim_simulation_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_simulation_tests COMMAND worldsim_simulation_tests)

    add_executable(worldsim_simulation_migration_tests tests/test_simulation_migration.cpp)
    target_link_libraries(worldsim_simulation_migration_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_simulation_migration_tests COMMAND worldsim_simulation_migration_tests)

    add_executable(worldsim_continental_water_tests tests/test_continental_water.cpp)
    target_link_libraries(worldsim_continental_water_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_continental_water_tests COMMAND worldsim_continental_water_tests)

    add_executable(worldsim_continental_water_index_tests tests/test_continental_water_index.cpp)
    target_link_libraries(worldsim_continental_water_index_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_continental_water_index_tests COMMAND worldsim_continental_water_index_tests)

    add_executable(worldsim_dynamic_hydrology_index_tests tests/test_dynamic_hydrology_index.cpp)
    target_link_libraries(worldsim_dynamic_hydrology_index_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_dynamic_hydrology_index_tests COMMAND worldsim_dynamic_hydrology_index_tests)

    add_executable(worldsim_dynamic_hydrology_c_api_atomicity_tests
        tests/test_dynamic_hydrology_c_api_atomicity.c)
    target_link_libraries(worldsim_dynamic_hydrology_c_api_atomicity_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_dynamic_hydrology_c_api_atomicity_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_dynamic_hydrology_c_api_atomicity_tests
        COMMAND worldsim_dynamic_hydrology_c_api_atomicity_tests)

    add_executable(worldsim_multiresolution_water_tests tests/test_multiresolution_water.cpp)
    target_link_libraries(worldsim_multiresolution_water_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_multiresolution_water_tests COMMAND worldsim_multiresolution_water_tests)

    add_executable(worldsim_refined_water_index_tests tests/test_refined_water_index.cpp)
    target_link_libraries(worldsim_refined_water_index_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_refined_water_index_tests COMMAND worldsim_refined_water_index_tests)

    add_executable(worldsim_multiresolution_water_persistence_tests
        tests/test_multiresolution_water_persistence.cpp)
    target_link_libraries(worldsim_multiresolution_water_persistence_tests PRIVATE worldsim::worldsim)
    add_test(NAME worldsim_multiresolution_water_persistence_tests
        COMMAND worldsim_multiresolution_water_persistence_tests)

    add_executable(worldsim_c_api_tests tests/test_c_api.c)
    target_link_libraries(worldsim_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_c_api_tests COMMAND worldsim_c_api_tests)

    add_executable(worldsim_soil_c_api_tests tests/test_soil_c_api.c)
    target_link_libraries(worldsim_soil_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_soil_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_soil_c_api_tests COMMAND worldsim_soil_c_api_tests)

    add_executable(worldsim_multiresolution_water_c_api_tests
        tests/test_multiresolution_water_c_api.c)
    target_link_libraries(worldsim_multiresolution_water_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_multiresolution_water_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_multiresolution_water_c_api_tests
        COMMAND worldsim_multiresolution_water_c_api_tests)

    add_executable(worldsim_weather_c_api_tests tests/test_weather_c_api.c)
    target_link_libraries(worldsim_weather_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_weather_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_weather_c_api_tests COMMAND worldsim_weather_c_api_tests)

    add_executable(worldsim_simulation_c_api_tests tests/test_simulation_c_api.c)
    target_link_libraries(worldsim_simulation_c_api_tests PRIVATE worldsim::worldsim)
    set_target_properties(worldsim_simulation_c_api_tests PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME worldsim_simulation_c_api_tests COMMAND worldsim_simulation_c_api_tests)

    add_test(NAME worldsim_cli_simulation_demo
        COMMAND worldsim_cli demo worldsim_cli_simulation.ws)
    add_test(NAME worldsim_cli_simulation_run
        COMMAND worldsim_cli simulation-run worldsim_cli_simulation.ws worldsim_cli_simulation.wsc 2)
    set_tests_properties(worldsim_cli_simulation_run PROPERTIES DEPENDS worldsim_cli_simulation_demo)
    add_test(NAME worldsim_cli_simulation_resume
        COMMAND worldsim_cli simulation-resume worldsim_cli_simulation.wsc 3)
    set_tests_properties(worldsim_cli_simulation_resume PROPERTIES DEPENDS worldsim_cli_simulation_run)
    add_test(NAME worldsim_cli_simulation_cleanup
        COMMAND ${CMAKE_COMMAND} -E rm -f worldsim_cli_simulation.ws worldsim_cli_simulation.wsc)
    set_tests_properties(worldsim_cli_simulation_cleanup PROPERTIES DEPENDS worldsim_cli_simulation_resume)
endif()