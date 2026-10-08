cmake_minimum_required(VERSION 3.8)
project(agt_livox_self_filter_bridge)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(livox_ros_driver2 REQUIRED)

add_executable(livox_self_filter_bridge_node src/livox_self_filter_bridge_node.cpp)
ament_target_dependencies(livox_self_filter_bridge_node rclcpp sensor_msgs livox_ros_driver2)
target_compile_features(livox_self_filter_bridge_node PUBLIC cxx_std_17)

install(TARGETS livox_self_filter_bridge_node DESTINATION lib/${PROJECT_NAME})
ament_package()
