cmake_minimum_required(VERSION 3.5)
project(http2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FetchContent)

# FetchContent_Declare(llhttp
#   URL "https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.0.1.tar.gz"
# )

# FetchContent_Declare(
#   googletest
#   GIT_REPOSITORY https://github.com/google/googletest.git
#   GIT_TAG        703bd9caab50b139428cea1aaff9974ebee5742e # release-1.10.0
# )

FetchContent_Declare(
  nghttp2
  URL "https://github.com/nghttp2/nghttp2/releases/download/v1.56.0/nghttp2-1.56.0.tar.gz"
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nghttp2

)

# set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(ENABLE_STATIC_LIB ON CACHE INTERNAL "")
FetchContent_MakeAvailable(nghttp2)

FetchContent_Declare(
  libevent
  URL "https://github.com/libevent/libevent/archive/refs/tags/release-2.1.12-stable.tar.gz"
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party/libevent

)

set(ENABLE_STATIC_LIB ON CACHE INTERNAL "")
FetchContent_MakeAvailable(libevent)


add_executable(http2 main.cpp)

target_include_directories(http2 PRIVATE third-party/nghttp2/lib/includes)

#target_link_libraries(http2 nghttp2 event libtcmalloc_minimal.a)
#target_link_libraries(http2 nghttp2 event libunwind-x86_64.a libunwind.a liblzma.a libtcmalloc_and_profiler.a)
#target_link_libraries(http2 nghttp2 event tcmalloc)
target_link_libraries(http2 nghttp2 event)



# find_package(llhttp REQUIRED COMPONENTS filesystem system)
# # Add an executable
# add_executable(http main.cpp)

# # Link with the llhttp_static target
# target_link_libraries(http llhttp::llhttp_static)
