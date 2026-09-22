cmake_minimum_required(VERSION 3.10)
project(PhModal)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT SC_PATH)
    message(FATAL_ERROR "SC_PATH must point at a SuperCollider source tree")
endif()
include_directories(
    ${SC_PATH}/include/plugin_interface
    ${SC_PATH}/include/common
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/modal-synth/include)
# upstream's own build option for the float type of its arithmetic
add_definitions(-DMODAL_NUM_TYPE=float)
# TWO builds: supernova loads only *_supernova.so, compiled with -DSUPERNOVA.
add_library(PhModal MODULE PhModal.cpp phmodal_core.cpp)
set_target_properties(PhModal PROPERTIES PREFIX "" SUFFIX ".so")
add_library(PhModal_supernova MODULE PhModal.cpp phmodal_core.cpp)
target_compile_definitions(PhModal_supernova PRIVATE SUPERNOVA=1)
set_target_properties(PhModal_supernova PROPERTIES PREFIX "" SUFFIX ".so")
