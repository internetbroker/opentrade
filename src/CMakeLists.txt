cmake_minimum_required(VERSION 3.10.0)
project(opentrade)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()
	
macro(subdirlist result)
  file(GLOB children ./*)
  set(dirlist "")
  foreach(child ${children})
    if(IS_DIRECTORY ${child})
      get_filename_component(name ${child} NAME)
      list(APPEND dirlist ${name})
    endif()
  endforeach()
  set(${result} ${dirlist})
endmacro()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -export-dynamic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -std=c++17")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

if(BACKTEST)
  message(STATUS "Building backtest")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBACKTEST")
endif()

if(UNIT_TEST)
  message(STATUS "Building unit_test")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DUNIT_TEST")
endif()

if(TEST_LATENCY)
  message(STATUS "Building test_latency")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTEST_LATENCY")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_path(QUICKFIX_INCLUDE_PATH quickfix/FixFields.h)
find_library(QUICKFIX_LIBRARY_PATH quickfix)
if(QUICKFIX_INCLUDE_PATH AND QUICKFIX_LIBRARY_PATH)
  message(STATUS "Found quickfix")
  include_directories(${QUICKFIX_INCLUDE_PATH})
else()
  message(FATAL_ERROR "quickfix not found.")
endif()

find_path(PQ_INCLUDE_PATH postgresql/libpq-fe.h)
if(PQ_INCLUDE_PATH)
  message(STATUS "Found pq-dev")
  include_directories(${PQ_INCLUDE_PATH}/postgresql)
else()
  find_path(PQ_INCLUDE_PATH libpq-fe.h)
  if(!PQ_INCLUDE_PATH)
    message(FATAL_ERROR "pq-dev not found.")
  endif()
endif()

find_path(SOCI_INCLUDE_PATH soci/soci.h)
find_library(SOCI_CORE_LIBRARY_PATH soci_core)
find_library(SOCI_POSTGRES_LIBRARY_PATH soci_postgresql)
find_library(SOCI_SQLITE3_LIBRARY_PATH soci_sqlite3)
if(SOCI_INCLUDE_PATH AND SOCI_CORE_LIBRARY_PATH)
  message(STATUS "Found soci")
  include_directories(${SOCI_INCLUDE_PATH}/soci)
else()
  message(FATAL_ERROR "soci not found.")
endif()

find_path(TBB_HPP_INCLUDE_PATH tbb/tbb.h)
find_library(TBB_LIBRARY_PATH tbb)
if(TBB_HPP_INCLUDE_PATH AND TBB_LIBRARY_PATH)
  message(STATUS "Found tbb")
  include_directories(${TBB_HPP_INCLUDE_PATH})
else()
  message(FATAL_ERROR "tbb not found.")
endif()

find_path(LOG4CXX_HPP_INCLUDE_PATH log4cxx/logger.h)
find_library(LOG4CXX_LIBRARY_PATH log4cxx)
if(LOG4CXX_HPP_INCLUDE_PATH AND LOG4CXX_LIBRARY_PATH)
  message(STATUS "Found log4cxx")
  include_directories(${LOG4CXX_HPP_INCLUDE_PATH})
else()
  message(FATAL_ERROR "log4cxx not found.")
endif()

find_package(PythonInterp 3 REQUIRED)
find_package(PythonLibs 3 REQUIRED)
include_directories(${PYTHON_INCLUDE_DIRS})
link_libraries(${PYTHON_LIBRARIES}) # Deprecated but so convenient!

find_package(Boost REQUIRED COMPONENTS system program_options date_time filesystem iostreams python3) 
include_directories({Boost_INCLUDE_DIRS})

set(EXE_DEPS 
  ${PROJECT_NAME}_static
  ${LOG4CXX_LIBRARY_PATH}
  ${QUICKFIX_LIBRARY_PATH}
  ${SOCI_CORE_LIBRARY_PATH}
  ${SOCI_POSTGRES_LIBRARY_PATH}
  ${SOCI_SQLITE3_LIBRARY_PATH}
  ${TBB_LIBRARY_PATH}
  ${Boost_LIBRARIES}
  dl pthread crypto
)

add_subdirectory(opentrade)
add_subdirectory(algos)
if(NOT BACKTEST)
add_subdirectory(adapters)
add_subdirectory(fix)
endif()
if(UNIT_TEST)
add_subdirectory(unit_test)
endif()
