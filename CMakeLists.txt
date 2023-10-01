cmake_minimum_required(VERSION 3.0)
project(tinyrpc)

# PROJECT_SOURCE_DIR ==> 
#       当使用project命令指定项目名称时，CMake会自动设置PROJECT_SOURCE_DIR的默认值为项目根目录的路径

SET(CMAKE_BUILD_TYPE "Debug") 
# 添加对gdb的支持
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

SET(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR})
enable_language(ASM)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -std=c++11 -Wall -Wno-deprecated -Wno-unused-but-set-variable")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -std=c++17 -Wall -Wno-deprecated -Wno-unused-but-set-variable")

set(PATH_LIB lib)
set(PATH_BIN bin)
set(PATH_TESTCASES testcases)

# include_directories  ==>
#       将指定目录添加到编译器的头文件搜索路径之下，指定的目录被解释成当前源码路径的相对路径
# include_directories 会为当前CMakeLists.txt的所有目标，以及之后添加的所有子目录的目标添加头文件搜索路径。
# 因此，慎用target_include_directories，因为会影响全局target。
include_directories(${PROJECT_SOURCE_DIR})
# *.cc
# aux_source_directory(<dir> <variable>)
# 收集指定目录中所有源文件的名称，并将列表(list)存储在提供的<variable>中
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/comm COMM)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/coroutine COR)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net NET)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net/http HTTP)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net/tcp TCP)
aux_source_directory(${PROJECT_SOURCE_DIR}/tinyrpc/net/tinypb TINYPB)

set(COXTX ${PROJECT_SOURCE_DIR}/tinyrpc/coroutine/coctx_swap.S)

add_library(tinyrpc ${COMM} ${COR} ${NET} ${HTTP} ${TCP} ${TINYPB} ${COXTX})
# install用于指定在安装时运行的规则。它可以用来安装很多内容，可以包括目标二进制、动态库、静态库以及文件、目录、脚本等
install(TARGETS tinyrpc DESTINATION ${PATH_LIB})


find_package(Protobuf REQUIRED)
if (${PROTOBUF_FOUND})
    message("protobuf found")
else ()
    message(FATAL_ERROR "Cannot find Protobuf")
endif ()

set(tinyxml /usr/lib/libtinyxml.a)

set(LIBS
    tinyrpc
    ${Protobuf_LIBRARY}
    ${tinyxml}
    dl
    pthread
)

# test_tinypb_server
set(
    test_tinypb_server
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_tinypb_server.cc
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_tinypb_server.pb.cc
)
add_executable(test_tinypb_server ${test_tinypb_server})
target_link_libraries(test_tinypb_server ${LIBS})
install(TARGETS test_tinypb_server DESTINATION ${PATH_BIN})

# test_tinypb_server_client
set(
    test_tinypb_server_client
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_tinypb_server_client.cc
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_tinypb_server.pb.cc
)
add_executable(test_tinypb_server_client ${test_tinypb_server_client})
target_link_libraries(test_tinypb_server_client ${LIBS})
install(TARGETS test_tinypb_server_client DESTINATION ${PATH_BIN})

# test_http_server
set(
    test_http_server
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_http_server.cc
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_tinypb_server.pb.cc
)
add_executable(test_http_server ${test_http_server})
target_link_libraries(test_http_server ${LIBS})
install(TARGETS test_http_server DESTINATION ${PATH_BIN})

# test_coroutine
set(
    test_coroutine
    ${PROJECT_SOURCE_DIR}/${PATH_TESTCASES}/test_coroutine.cc
)
add_executable(test_coroutine ${test_coroutine})
target_link_libraries(test_coroutine ${LIBS})
install(TARGETS test_coroutine DESTINATION ${PATH_BIN})

# install *.h
add_subdirectory(tinyrpc/comm)
add_subdirectory(tinyrpc/coroutine)
add_subdirectory(tinyrpc/net)
add_subdirectory(tinyrpc/net/http)
add_subdirectory(tinyrpc/net/tcp)
add_subdirectory(tinyrpc/net/tinypb)