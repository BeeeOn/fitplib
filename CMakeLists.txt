cmake_minimum_required (VERSION 2.8.11)
project (fitprotocol-library CXX)

if(DEFINED ENV{GIT_ID})
	set (GIT_ID $ENV{GIT_ID})
else()
	exec_program(
		"git"
		${CMAKE_CURRENT_SOURCE_DIR}
		ARGS "describe --always --tags"
		OUTPUT_VARIABLE GIT_ID)
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
	message("Debug build")
	add_definitions(-D_DEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES "Release")
	message("Release build")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(GLIB REQUIRED)
include_directories(
	fitp
	${GLIB_INCLUDE_DIRS}
)

add_definitions(-DGIT_ID="${GIT_ID}" -std=c++11 -Wall -pedantic -Wextra -lfitp)

add_subdirectory(fitp)

set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "9")
set(CPACK_PACKAGE_VERSION_PATCH "1")
set(CPACK_PACKAGE_NAME "fitplib")
set(CPACK_PACKAGE_VENDOR "Brno University of Technology")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
	"FIT Protocol - experimental sensoric network protocol implementation")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_RPM_PACKAGE_LICENSE "BSD")
include(CPack)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/fitp/
DESTINATION include/fitp
FILES_MATCHING PATTERN "*.h")
