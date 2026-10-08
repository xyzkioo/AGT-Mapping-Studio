cmake_minimum_required(VERSION 3.8)
project(agt_livox_self_return_filter)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(livox_ros_driver2 REQUIRED)

add_executable(livox_self_return_filter_node src/livox_self_return_filter_node.cpp)
ament_target_dependencies(livox_self_return_filter_node rclcpp livox_ros_driver2)

install(TARGETS livox_self_return_filter_node
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch/ DESTINATION share/${PROJECT_NAME}/launch)

ament_package()
