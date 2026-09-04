cmake_minimum_required(VERSION 3.15)

project(Calculus LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA REQUIRED lua)

add_executable(calculus 
    src/main.c
    src/crypto.c    
)

target_include_directories(calculus PRIVATE include)
target_include_directories(calculus PRIVATE ${LUA_INCLUDE_DIRS})
target_link_libraries(calculus PRIVATE ${LUA_LIBRARIES})