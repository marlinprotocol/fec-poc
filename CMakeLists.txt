cmake_minimum_required(VERSION 3.9)
project(fec VERSION 0.0.1 LANGUAGES CXX)

add_subdirectory(3rdparty/wirehair)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror -Wall -Wextra -pedantic-errors")

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIR})
include_directories(3rdparty/wirehair)

add_executable(main src/main.cpp)
target_link_libraries(main pthread wirehair-shared)

#include(GNUInstallDirs)
#
#list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
#
#add_library(fec STATIC
#	src/main.cpp
#)
#
target_include_directories(main
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${BOOST_INCLUDEDIR}
#	PRIVATE
#		${CMAKE_CURRENT_SOURCE_DIR}/src
)
#
#find_library(boost REQUIRED)
#
#find_library(wirehair REQUIRED)
#target_link_libraries(fec PUBLIC wirehair)
#
#add_executable(main
#	src/main.cpp
#)
#
#target_include_directories(main
#	PUBLIC
#		${CMAKE_CURRENT_SOURCE_DIR}/include
#	PRIVATE
#		${CMAKE_CURRENT_SOURCE_DIR}/src
#)
#
#target_link_libraries(main PUBLIC fec)
#
