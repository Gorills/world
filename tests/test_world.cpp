#define main worldsim_test_main
#include "test_world_part1.inc"
#include "test_world_part2.inc"
#include "test_world_part3.inc"
#undef main

#include <exception>
#include <iostream>

int main() {
    try {
        return worldsim_test_main();
    } catch (const std::exception& e) {
        std::cerr << "UNCAUGHT worldsim_tests exception: " << e.what() << '\n';
        return 2;
    } catch (...) {
        std::cerr << "UNCAUGHT worldsim_tests non-standard exception\n";
        return 3;
    }
}
