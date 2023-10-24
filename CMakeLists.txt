cmake_minimum_required(VERSION 3.26)
project(webserver C)

set(CMAKE_C_STANDARD 23)

add_executable(webserver main.c)

target_link_libraries(webserver Ws2_32)
