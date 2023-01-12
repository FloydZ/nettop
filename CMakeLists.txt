cmake_minimum_required(VERSION 3.20.1)
project(decoding)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_VERBOSE_MAKEFILE ON)


set(ALLOWED_WARNINGS "-Wno-format")

set(DEBUG_FLAGS "-DDEBUG -Werror -Wall -Wpedantic -pedantic -pthread ${ALLOWED_WARNINGS}")
set(OPTIMIZE_FLAGS "-Ofast -Werror -Wall -Wpedantic -pedantic -ffast-math -ftree-vectorize -funroll-loops -Wall -Wpedantic -pedantic -march=native -flto -fwrapv -fomit-frame-pointer -pthread ${ALLOWED_WARNINGS}")

# Set additionally our own debug/optimisation compiler/linkage flags. Maybe not he nicest way, but by far the easiest.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${DEBUG_FLAGS}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${DEBUG_FLAGS}")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${OPTIMIZE_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPTIMIZE_FLAGS}")


#set(SRC src/*.cpp)
file(GLOB SRC src/*.cpp)

add_executable(main ${SRC})
target_link_libraries(main pcap curses)
