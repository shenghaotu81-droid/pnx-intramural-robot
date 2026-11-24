add_executable(controller_sim simulation.cpp)
target_link_libraries(controller_sim PRIVATE controller)

foreach(scenario IN ITEMS mark tag-align tag-retreat search yaw-hold feedforward-bias)
    add_test(NAME controller_sim_${scenario} COMMAND controller_sim ${scenario})
endforeach()

add_executable(controller_config_load_test config_load_test.cpp)
target_link_libraries(controller_config_load_test PRIVATE controller)
add_test(NAME controller_config_load_test COMMAND controller_config_load_test
         WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
