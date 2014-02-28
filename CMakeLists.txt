# CMake Version
CMAKE_MINIMUM_REQUIRED(VERSION 2.8 FATAL_ERROR)

# CMake ExternalProject module
INCLUDE(ExternalProject)

# Build Type
SET(CMAKE_BUILD_TYPE Debug)
# Build Type Option
SET(CMAKE_C_FLAGS_RELEASE "-Wall -O2")
SET(CMAKE_C_FLAGS_DEBUG "-W -g")
SET(CMAKE_C_FLAGS_COVERAGE "-W -g -fprofile-arcs -ftest-coverage")
SET(CMAKE_EXE_LINKER_FLAGS_COVERAGE "-fprofile-arcs -ftest-coverage")

# Project: ZeroMQ tools
PROJECT(zmq-tools)
SET(MAJOR_VERSION 0)
SET(MINOR_VERSION 0)
SET(BUILD_VERSION 1)
#SET(REVISION_VERSION 0)

# Option
OPTION(USE_SYSTEM_ZEROMQ "Use system zeromq" ON)

# Config
SET(PIDFILE "/var/run/zmqd.pid")

# ZeroMQ (>= 4.0)
MESSAGE(STATUS "Use system ZeroMQ library: ${USE_SYSTEM_ZEROMQ}")
SET(ZEROMQ_MINIMUM_REQUIRED_VERSION 40000)
IF(USE_SYSTEM_ZEROMQ)
  FIND_PATH(ZEROMQ_INCLUDES
    zmq.h
    PATHS ${ZEROMQ_INCLUDE_PATH} /usr/include)
  IF(ZEROMQ_INCLUDES STREQUAL "ZEROMQ_INCLUDES-NOTFOUND")
    MESSAGE(FATAL_ERROR "ZeroMQ could not found zmq.h\n"
      "OPTION: -DZEROMQ_INCLUDE_PATH=path")
  ENDIF()
  INCLUDE_DIRECTORIES(${ZEROMQ_INCLUDES})

  FIND_LIBRARY(ZEROMQ_LIBRARIES
    NAMES zmq
    PATHS ${ZEROMQ_LIBRARY_PATH} /usr/lib64 /usr/lib)
  IF(ZEROMQ_LIBRARIES STREQUAL "ZEROMQ_LIBRARIES-NOTFOUND")
    MESSAGE(FATAL_ERROR "ZeroMQ could not found libzmq.so\n"
      "OPTION: -DZEROMQ_LIBRARY_PATH=path")
  ENDIF()
ELSE()
  ExternalProject_ADD(
    zeromq
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/zeromq-4.0.3
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/deps
    CONFIGURE_COMMAND
    <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
    --with-pic --with-gnu-ld --without-documentation --enable-static=yes)
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR}/deps/include)
  SET(ZEROMQ_LIBRARIES
    ${CMAKE_CURRENT_BINARY_DIR}/deps/lib/libzmq.a stdc++ pthread rt)
  SET(DEPS zeromq)
ENDIF()

INCLUDE(CheckCSourceRuns)
CHECK_C_SOURCE_RUNS("
    #include <zmq.h>
    int main() {
        if (ZMQ_VERSION > ${ZEROMQ_MINIMUM_REQUIRED_VERSION}) {
            return 0;
        } else {
            return 1;
        }
    }" HAVE_ZMQ_VERSION)
IF(NOT HAVE_ZMQ_VERSION)
  MESSAGE(FATAL_ERROR "ZeroMQ 4.0 or higher is required.")
ENDIF()

# Configure
CONFIGURE_FILE(
  ${PROJECT_SOURCE_DIR}/src/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})

# Execute
ADD_EXECUTABLE(zmq-server
  ${PROJECT_SOURCE_DIR}/src/server.c ${PROJECT_SOURCE_DIR}/src/utils.c)
ADD_EXECUTABLE(zmq-client
  ${PROJECT_SOURCE_DIR}/src/client.c ${PROJECT_SOURCE_DIR}/src/utils.c)

# Libraries
TARGET_LINK_LIBRARIES(zmq-server ${ZEROMQ_LIBRARIES})
TARGET_LINK_LIBRARIES(zmq-client ${ZEROMQ_LIBRARIES} pthread)
IF(DEPS)
  ADD_DEPENDENCIES(zmq-server ${DEPS})
  ADD_DEPENDENCIES(zmq-client ${DEPS})
ENDIF()

# Install
INSTALL_PROGRAMS(/bin FILES ${CMAKE_CURRENT_BINARY_DIR}/zmq-server)
INSTALL_PROGRAMS(/bin FILES ${CMAKE_CURRENT_BINARY_DIR}/zmq-client)
