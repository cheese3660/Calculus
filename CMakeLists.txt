cmake_minimum_required(VERSION 3.15)

project(Calculus LANGUAGES C)
add_compile_options(-Wall -Wextra -Werror)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

# Statically link lua
pkg_check_modules(LUA REQUIRED lua)

add_executable(calculus 
    src/calculus_main.c
    src/common/crypto.c
    src/common/archive.c # Used for moving stuff between stores/caches
)

add_executable(car
    src/car_main.c
    src/common/crypto.c # Used still for the hashing
    src/common/archive.c # The main archival/dearchival entrypoint
)

target_include_directories(calculus PRIVATE include)
target_include_directories(car PRIVATE include)

# Different projects need different things, as we might have multiple executables
target_include_directories(calculus PRIVATE ${LUA_INCLUDE_DIRS})
target_link_libraries(calculus PRIVATE ${LUA_LIBRARIES})