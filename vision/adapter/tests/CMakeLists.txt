add_executable(adapter_test adapter_test.cpp)
target_link_libraries(adapter_test PRIVATE adapter)

add_test(NAME adapter_test COMMAND adapter_test)

add_executable(adapter_config_load_test config_load_test.cpp)
target_link_libraries(adapter_config_load_test PRIVATE adapter)
add_test(NAME adapter_config_load_test COMMAND adapter_config_load_test
         WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
