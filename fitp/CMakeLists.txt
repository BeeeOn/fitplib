cmake_minimum_required (VERSION 2.8.11)
project (fitp CXX)

file(GLOB SOURCES
	${PROJECT_SOURCE_DIR}/pan/fitp/fitp.cpp
	${PROJECT_SOURCE_DIR}/pan/global_storage/global.cpp
	${PROJECT_SOURCE_DIR}/pan/net_layer/net.cpp
	${PROJECT_SOURCE_DIR}/pan/link_layer/link.cpp
	${PROJECT_SOURCE_DIR}/pan/phy_layer/phy.cpp
)

file(GLOB TESTING_SOURCES
	${PROJECT_SOURCE_DIR}/core/TestingCenter.cpp
)

include_directories(
	${CMAKE_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/fitp/
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

set(LIBS
	${GLIB_LIBRARIES}
)

target_link_libraries(${PROJECT_NAME}
	${LIBS}
)

install(TARGETS ${PROJECT_NAME} DESTINATION lib/)
