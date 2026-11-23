cmake_minimum_required(VERSION 3.16)
project(pnx_vision)

include(CTest)
add_subdirectory(pipeline)
add_subdirectory(controller)
add_subdirectory(adapter)
