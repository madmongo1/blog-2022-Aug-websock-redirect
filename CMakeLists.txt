cmake_minimum_required(VERSION 3.21)

include(FetchContent)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

message(STATUS "[dependency] building fmt")
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 9.0.0
    GIT_SHALLOW ON
    GIT_PROGRESS ON
)
FetchContent_MakeAvailable(fmt)

project(blog-2022-aug-websock-redirect VERSION 0.1)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

configure_file(src/cert/ca.pem ${CMAKE_CURRENT_BINARY_DIR}/ca.pem)
configure_file(src/cert/dh4096.pem ${CMAKE_CURRENT_BINARY_DIR}/dh4096.pem)
configure_file(src/cert/server.pem ${CMAKE_CURRENT_BINARY_DIR}/server.pem)

file(GLOB_RECURSE srcs CONFIGURE_DEPENDS "src/*.[ch]pp")
list(APPEND CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(${PROJECT_NAME} ${srcs})
target_link_libraries(${PROJECT_NAME}
    Boost::system
    fmt::fmt
    OpenSSL::Crypto OpenSSL::SSL
    Threads::Threads
    )
