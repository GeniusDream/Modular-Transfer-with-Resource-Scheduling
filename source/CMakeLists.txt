cmake_minimum_required(VERSION 3.20)
project(Test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-PIC -mcmodel=large -O")

include_directories(/usr/include)

# lz4 and threadPool
set(PROJECT_LINK_LIBS liblz4.so libpthread.so libbz2.so)
find_package(ZLIB)

# CMake module path
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# main.cpp
add_executable(test_queue "main(queue).cpp" ${EXAMPLE_HEADER_FILES} ${EXAMPLE_INLINE_FILES} ThreadPool.h thpool.h thpool.cpp gzcompress.h gzcompress.cpp thread_safe_queue.h httplib.h cpuTest.h)
target_link_libraries(test_queue ${LINKLIBS} ${PROJECT_LINK_LIBS} ${ZLIB_LIBRARIES})

add_executable(test_single "main(single).cpp" ${EXAMPLE_HEADER_FILES} ${EXAMPLE_INLINE_FILES} ThreadPool.h thpool.h thpool.cpp gzcompress.h gzcompress.cpp thread_safe_queue.h httplib.h cpuTest.h)
target_link_libraries(test_single ${LINKLIBS} ${PROJECT_LINK_LIBS} ${ZLIB_LIBRARIES})

