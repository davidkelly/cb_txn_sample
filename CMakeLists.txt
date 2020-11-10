cmake_minimum_required(VERSION 3.9)

project(transactions_cxx)
set(CMAKE_CXX_STANDARD 11)

#########################################################
#
# to use transactions and the corresponding client, we
# depend on several things.
#
# * libtransactions_cxx
# * libclient_cxx
#
# for these, easiest to pop them in /usr/lib, /usr/include
# or the equivalent.  But you can always place them elsewhere
# and call cmake with the appropriate -DXXX for your situation
#
# For instance, something like:
# cmake -DCMAKE_CXX_FLAGS='-I <path-to-includes>' -DCMAKE_PREFIX_PATH=<path-to-libs>
#
# * libcouchbase
# * boost
#
# Easiest to just install these - any recent boost should be fine
# but we tested with 1.67.0 on our build/test machines
#
# * spdlog
# * nlohmann::json
#
# these are best to just grab.  Below, I did so as external projects
# but you can do the more modern FetchContent, or make them git submodules
# in your project, etc...
#
# Also, we depend on threads (pthread, etc...).  That is part of glibc, so
# you should already have it.
#

find_package(THREADS)

# lets find the libraries we depend on
find_library(LIB_TXN transactions_cxx REQUIRED)

# lets assume we have installed boost, or point to it
# via -DBOOST_ROOT when calling cmake
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS OFF)
include(FindBoost)
find_package(Boost REQUIRED COMPONENTS system thread date_time)
include_directories(BEFORE ${Boost_INCLUDE_DIRS})
message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_LIBRARIES: ${Boost_LIBRARIES}")

# External dependencies - spdlog and nlohmann::json
#
# You will have to use these libraries whenever you use couchbase
# transactions library (or the client).
#
# spdlog can be installed, or FetchContent can be used, but this
# is for older cmake
include(ExternalProject)
ExternalProject_Add(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.7.0
    INSTALL_DIR ${PROJECT_BINARY_DIR}/external
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
)

include_directories(${PROJECT_BINARY_DIR}/external/include)

# For nlohmann::json, their github page shows how to do it for later cmake with FetchContent
# support, etc...  This is for older cmake
ExternalProject_Add(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.9.1
    INSTALL_DIR ${PROJECT_BINARY_DIR}/external
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DBUILD_TESTING=0 -DJSON_MultipleHeaders=ON
)

# now just add the example executable
add_executable(txn_example txn_example.cxx)

target_link_libraries(txn_example ${CMAKE_THREAD_LIBS_INIT} ${LIB_TXN} ${Boost_LIBRARIES} )

# since these are external, use add_dependencies
add_dependencies(txn_example spdlog json)
