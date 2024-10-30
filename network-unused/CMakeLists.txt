# CMakeLists files in this project can
# refer to the root source directory of the project as ${HELLO_SOURCE_DIR} and
# to the root binary directory of the project as ${HELLO_BINARY_DIR}.
cmake_minimum_required (VERSION 2.8.11)
project (network_benchmark)

link_directories($ENV{HOME}/workspace/replication-protos/eRPC/build)
link_directories($ENV{HOME}/workspace/replication-protos/dpdk-stable-19.11.14/usr/lib)
include_directories($ENV{HOME}/workspace/replication-protos/eRPC/src)
include_directories($ENV{HOME}/workspace/replication-protos/network_bench/net_sgx)
include_directories($ENV{HOME}/workspace/replication-protos/dpdk-stable-19.11.14/x86_64-native-linuxapp-gcc/include)

SET(CMAKE_CXX_FLAGS  "-DERPC_DPDK=true -g -fsanitize=address --std=c++20")
find_library(FMT_lib fmt)
if(NOT FMT_lib)
    message(STATUS "fmt library not found")
endif()



#add_executable(bench_donna main.cc net_sgx/shared.cpp $ENV{HOME}/workspace/replication-protos/eRPC/build/src/config.h)
#add_executable(bench_martha main.cc net_sgx/shared.cpp $ENV{HOME}/workspace/replication-protos/eRPC/build/src/config.h)
add_executable(bench main.cc net_sgx/shared.cpp $ENV{HOME}/workspace/replication-protos/eRPC/build/src/config.h)



#target_link_libraries(bench_donna -lpthread -lfmt -lnuma -lerpc -lgflags -ldl -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive -lrte_ethdev -Wl,-lrte_port -fsanitize=address)
target_link_libraries(bench -lpthread -lfmt -lnuma -lerpc -lgflags -ldl -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive -lrte_ethdev -Wl,-lrte_port -fsanitize=address)
#target_link_libraries(bench_martha -lpthread -lfmt -lnuma -lerpc -lgflags -ldl -Wl,--whole-archive -ldpdk -Wl,--no-whole-archive -lrte_ethdev -Wl,-lrte_port -fsanitize=address)



