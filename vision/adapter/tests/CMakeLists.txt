add_executable(adapter_test adapter_test.cpp)
target_link_libraries(adapter_test PRIVATE adapter)

add_test(NAME adapter_test COMMAND adapter_test)
