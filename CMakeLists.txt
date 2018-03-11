cmake_minimum_required(VERSION 3.6)
project(evsocks)

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
add_definitions(-Wall -Wextra -g)
add_definitions(-O2)

set(SRC_UTIL
    src/string_util.hpp src/misc.hpp
    src/ctxlog/framework.hpp src/ctxlog/ctxlog_evsocks.hpp
    src/dlist.hpp src/timeout_list.hpp
    src/stb_sprintf.c
)
set(SRCS
    src/main.cpp src/server.cpp src/auth.cpp src/addr.cpp src/bufqueue.cpp
    src/net.cpp src/iochannel.cpp src/error.h
)

add_executable(evsocks ${SRCS} ${SRC_UTIL})

find_library(LIBEV NAMES libev.a ev)
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost COMPONENTS thread)
target_link_libraries(evsocks
    ${LIBEV} ${Boost_LIBRARIES}
    -static-libgcc -static-libstdc++ -static
)
