CMAKE_MINIMUM_REQUIRED (VERSION 2.8)
PROJECT (replicator)

set(CMAKE_VERBOSE_MAKEFILE on)
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY true)

set(REPLICATOR_NAME "replicatord")
set(REPLICATOR_ROOT "${CMAKE_SOURCE_DIR}")
set(REPLICATOR_CFLAGS "-DTB_LOCAL=${REPLICATOR_ROOT}/lib/tarantool-c/lib -std=c++0x -g")
set(REPLICATOR_SRC
    ${REPLICATOR_ROOT}/lib/tarantool-c/lib/session.c
    ${REPLICATOR_ROOT}/dbreader.cpp
    ${REPLICATOR_ROOT}/main.cpp
    ${REPLICATOR_ROOT}/tpwriter.cpp
)

add_subdirectory(lib/tarantool-c)
add_subdirectory(lib/libslave)

include(ExternalProject)
ExternalProject_Add(libconfig
  SOURCE_DIR ${REPLICATOR_ROOT}/lib/libconfig/
  CONFIGURE_COMMAND ${REPLICATOR_ROOT}/lib/libconfig/configure --prefix=${CMAKE_CURRENT_SOURCE_DIR}/lib/libconfig
  PREFIX ${REPLICATOR_ROOT}/lib/libconfig
  BUILD_COMMAND make
  BUILD_IN_SOURCE 1
)

include_directories("${REPLICATOR_ROOT}" "${REPLICATOR_ROOT}/lib/libslave" "${REPLICATOR_ROOT}/lib/msgpuck" "${REPLICATOR_ROOT}/lib/tarantool-c" "${REPLICATOR_ROOT}/lib/libconfig/include")

find_library(LMYSQL_CLIENT_R mysqlclient_r PATH_SUFFIXES mysql)
find_library(LPTHREAD pthread)
find_library(LZMQ zmq)
find_library(LBOOST_SYSTEM_MT boost_system-mt)
find_library(LBOOST_SERIALIZATION_MT boost_serialization-mt)

add_executable(rp ${REPLICATOR_SRC})
set_target_properties(rp PROPERTIES COMPILE_FLAGS "${REPLICATOR_CFLAGS}")
set_target_properties(rp PROPERTIES OUTPUT_NAME ${REPLICATOR_NAME})
target_link_libraries(rp tb slave_a ${REPLICATOR_ROOT}/lib/libconfig/lib/.libs/libconfig++.a)
target_link_libraries(rp ${LMYSQL_CLIENT_R} ${LPTHREAD} ${LZMQ} ${LBOOST_SYSTEM_MT} ${LBOOST_SERIALIZATION_MT})

install(TARGETS rp RUNTIME DESTINATION sbin)
install(FILES replicatord.cfg DESTINATION etc)