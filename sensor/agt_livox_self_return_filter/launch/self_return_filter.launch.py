from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('input_topic', default_value='/agt/sensors/lidar/custom'),
        DeclareLaunchArgument('output_topic', default_value='/mapping/sensor/livox_prefiltered'),
        Node(
            package='agt_livox_self_return_filter',
            executable='livox_self_return_filter_node',
            name='livox_self_return_filter_node',
            output='screen',
            parameters=[{
                'input_topic': LaunchConfiguration('input_topic'),
                'output_topic': LaunchConfiguration('output_topic'),
                'box_min': [-0.82, -0.18, -0.10],
                'box_max': [-0.48, 0.18, 0.70],
                'report_every_scans': 100,
            }],
        ),
    ])
