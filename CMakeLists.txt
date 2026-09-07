cmake_minimum_required(VERSION 3.15)

project(Calculus LANGUAGES C)
add_compile_options(-Wall -Wextra -Werror)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(LUA REQUIRED IMPORTED_TARGET lua)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)

add_executable(calculus 
    src/calculus_main.c
    
    src/common/crypto.c
    src/common/archive.c # Used for moving stuff between stores/caches
    src/common/command.c
    src/common/fs.c

    src/calculus/luaenv.c
    src/calculus/gittools.c
    src/calculus/derivative.c
    src/calculus/build.c

    src/thirdparty/stb_ds.c
)

add_executable(car
    src/car_main.c
    src/common/crypto.c # Used still for the hashing
    src/common/archive.c # The main archival/dearchival entrypoint
)

target_include_directories(calculus PRIVATE include)
target_include_directories(car PRIVATE include)

# Different projects need different things, as we might have multiple executables
# target_include_directories(calculus PRIVATE ${LUA_INCLUDE_DIRS})
# target_link_libraries(calculus PRIVATE ${LUA_LIBRARIES})

target_link_libraries(calculus PRIVATE PkgConfig::LUA PkgConfig::LIBGIT2 PkgConfig::LIBARCHIVE PkgConfig::LIBCURL)