"""Shared mapping processes for bag replay and live capture."""
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from .preflight import frontend_remappings
from .self_filter_launch import (
    VEHICLE_RETURN_TOPIC, adapter_parameters, self_filter_nodes, vehicle_return_nodes)


def mapping_processes(share, output, source, use_sim_time, self_filter, vehicle_filter):
    """Return (process, failure label) pairs in sensor-to-exporter order."""
    if self_filter.enabled and vehicle_filter.enabled:
        raise ValueError('Select either robot self filter or vehicle return filter, not both')
    text = lambda value: ParameterValue(str(value), value_type=str)
    lio_config = share / 'config' / 'fastlio2_mid360.yaml'
    processes = [(node, 'Livox vehicle return filter') for node in
                 vehicle_return_nodes(vehicle_filter, source.lidar_topic, use_sim_time)]
    processes.append((Node(
        package='agt_mid360_adapter', executable='mid360_adapter_node',
        parameters=[adapter_parameters(
            VEHICLE_RETURN_TOPIC if vehicle_filter.enabled else source.lidar_topic,
            self_filter)]), 'sensor adapter'))
    processes.extend(zip(self_filter_nodes(self_filter, use_sim_time),
                         ('robot self filter', 'Livox self-filter bridge')))
    processes.extend([
        (Node(package='fastlio2', namespace='fastlio2', executable='lio_node',
              parameters=[{'config_path': text(lio_config), 'use_sim_time': use_sim_time}],
              remappings=frontend_remappings(lio_config, source.imu_topic)), 'FAST-LIO2'),
        (Node(package='agt_fastlio_backend', executable='fastlio_backend_node',
              parameters=[{'use_sim_time': use_sim_time}]), 'frontend bridge'),
        (Node(package='pgo', executable='pgo_node',
              parameters=[{'config_path': text(share / 'config' / 'pgo_frontend.yaml'),
                           'use_sim_time': use_sim_time}]), 'PGO'),
        (Node(package='agt_pgo_backend', executable='pgo_backend_node',
              parameters=[{'use_sim_time': use_sim_time,
                           'pgo_output_dir': text(output / 'pgo_raw')}]), 'PGO bridge'),
        (Node(package='agt_mapping_exporter', executable='mapping_artifact_exporter',
              parameters=[{'use_sim_time': use_sim_time,
                           'output_dir': text(output)}]), 'artifact exporter'),
    ])
    return processes


def mapping_rviz(share, use_sim_time):
    return Node(
        package='rviz2', executable='rviz2', name='agt_mapping_rviz', output='screen',
        arguments=['-d', str(share / 'rviz' / 'mapping_v0.rviz')],
        parameters=[{'use_sim_time': True}] if use_sim_time else [],
        additional_env={'SNAP': '', 'SNAP_LIBRARY_PATH': '', 'GTK_PATH': '',
                        'GTK_EXE_PREFIX': '', 'GIO_MODULE_DIR': '', 'GTK_IM_MODULE_FILE': ''},
    )
