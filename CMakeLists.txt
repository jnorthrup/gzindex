cmake_minimum_required(VERSION 3.0)
project(gzindex)

find_package(ZLIB REQUIRED)

add_executable(gzindex gzindex.c)
target_link_libraries(gzindex PRIVATE ${ZLIB_LIBRARIES} m)

install(TARGETS gzindex DESTINATION bin)
