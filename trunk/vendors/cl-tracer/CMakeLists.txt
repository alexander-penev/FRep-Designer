cmake_minimum_required(VERSION 2.8)

project(frep-cl-tracer)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake-modules")

# Embedded GLM lib
set(GLM_ROOT_DIR "./vendors/glm")


find_package(OpenCL REQUIRED)
find_package(GiNaC REQUIRED)
find_package(GLM REQUIRED)

add_subdirectory(vendors)

set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pedantic -fPIC -arch i386 -arch x86_64" )
set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -fPIC -arch i386 -arch x86_64" )

set(CMAKE_BUILD_TYPE Debug CACHE STRING "default to debug" FORCE)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${OPENCL_INCLUDE_DIRS} ${CLN_INCLUDE_DIR} ${GINAC_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS})

set(LIBRARY_DEPS ${OPENCL_LIBRARIES} ${CLN_LIBRARIES} ${GINAC_LIBRARIES})

aux_source_directory("${PROJECT_SOURCE_DIR}/src/." SRC_LIST)
file(GLOB INCLUDE_LIST "${PROJECT_SOURCE_DIR}/include/*.h")
include_directories("${PROJECT_SOURCE_DIR}/include/")

add_library(${PROJECT_NAME} SHARED ${SRC_LIST} ${INCLUDE_LIST})

#link the module like any other
target_link_libraries(${PROJECT_NAME} ${LIBRARY_DEPS})

add_executable("${PROJECT_NAME}.bin" ${SRC_LIST} "${PROJECT_SOURCE_DIR}/main.cpp")
target_link_libraries("${PROJECT_NAME}.bin" ${LIBRARY_DEPS})
