cmake_minimum_required(VERSION 3.21)

include(FetchContent)


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

if (NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl")
endif()

FetchContent_Declare(boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG boost-1.80.0.beta1
    GIT_SUBMODULES_RECURSE TRUE
    GIT_PROGRESS TRUE
)
message(STATUS "[dependency] building boost")
FetchContent_MakeAvailable(boost)

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


file(GLOB_RECURSE srcs CONFIGURE_DEPENDS "src/*.[ch]pp")
list(APPEND CMAKE_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_executable(${PROJECT_NAME} ${srcs})
target_link_libraries(${PROJECT_NAME}
    Boost::asio Boost::beast Boost::system
    fmt::fmt
    OpenSSL::Crypto OpenSSL::SSL
    Threads::Threads
    )
