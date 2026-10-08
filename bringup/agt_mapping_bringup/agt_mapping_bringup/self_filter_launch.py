"""Optional robot_self_filter pipeline for MID360 mapping."""
from dataclasses import dataclass
import math
import re

from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

from .preflight import PreflightError, parse_bool, validate_number


@dataclass(frozen=True)
class SelfFilterSettings:
    enabled: bool
    frame: str
    size: tuple[float, float, float]
    offset: tuple[float, float, float]
    padding: float


@dataclass(frozen=True)
class VehicleReturnSettings:
    enabled: bool
    box_min: tuple[float, float, float]
    box_max: tuple[float, float, float]


VEHICLE_RETURN_TOPIC = '/mapping/sensor/livox_prefiltered'


def declare_vehicle_return_arguments():
    return [
        DeclareLaunchArgument('vehicle_return_filter_enabled', default_value='false'),
        DeclareLaunchArgument('vehicle_return_box_min_x', default_value='-0.82'),
        DeclareLaunchArgument('vehicle_return_box_min_y', default_value='-0.18'),
        DeclareLaunchArgument('vehicle_return_box_min_z', default_value='-0.10'),
        DeclareLaunchArgument('vehicle_return_box_max_x', default_value='-0.48'),
        DeclareLaunchArgument('vehicle_return_box_max_y', default_value='0.18'),
        DeclareLaunchArgument('vehicle_return_box_max_z', default_value='0.70'),
    ]


def read_vehicle_return_settings(context):
    def value(name):
        return LaunchConfiguration(name).perform(context)

    enabled = parse_bool(value('vehicle_return_filter_enabled'))
    bounds = []
    for bound in ('min', 'max'):
        coordinates = []
        for axis in 'xyz':
            name = f'vehicle_return_box_{bound}_{axis}'
            try:
                coordinate = float(value(name))
            except (TypeError, ValueError) as exc:
                raise PreflightError(f'{name} must be a finite number') from exc
            if not math.isfinite(coordinate):
                raise PreflightError(f'{name} must be a finite number')
            coordinates.append(coordinate)
        bounds.append(tuple(coordinates))
    box_min, box_max = bounds
    if any(lo >= hi for lo, hi in zip(box_min, box_max)):
        raise PreflightError('vehicle return box min must be below max on every axis')
    return VehicleReturnSettings(enabled, box_min, box_max)


def vehicle_return_nodes(settings, input_topic, use_sim_time):
    if not settings.enabled:
        return []
    return [Node(
        package='agt_livox_self_return_filter',
        executable='livox_self_return_filter_node',
        name='livox_self_return_filter_node',
        output='screen',
        parameters=[{
            'use_sim_time': use_sim_time,
            'input_topic': input_topic,
            'output_topic': VEHICLE_RETURN_TOPIC,
            'box_min': list(settings.box_min),
            'box_max': list(settings.box_max),
        }],
    )]


def declare_self_filter_arguments():
    return [
        DeclareLaunchArgument('self_filter_enabled', default_value='false'),
        DeclareLaunchArgument('self_filter_frame', default_value='livox_frame'),
        DeclareLaunchArgument('self_filter_size_x', default_value='0.0'),
        DeclareLaunchArgument('self_filter_size_y', default_value='0.0'),
        DeclareLaunchArgument('self_filter_size_z', default_value='0.0'),
        DeclareLaunchArgument('self_filter_offset_x', default_value='0.0'),
        DeclareLaunchArgument('self_filter_offset_y', default_value='0.0'),
        DeclareLaunchArgument('self_filter_offset_z', default_value='0.0'),
        DeclareLaunchArgument('self_filter_padding', default_value='0.02'),
    ]


def read_self_filter_settings(context):
    def value(name):
        return LaunchConfiguration(name).perform(context)

    enabled = parse_bool(value('self_filter_enabled'))
    frame = value('self_filter_frame')
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', frame):
        raise PreflightError('self_filter_frame must be a URDF-safe frame name without a leading slash')
    size = tuple(validate_number(value(f'self_filter_size_{axis}'), f'self_filter_size_{axis}',
                                 allow_zero=not enabled)
                 for axis in 'xyz')
    if enabled and any(component <= 0.0 for component in size):
        raise PreflightError('self filter is enabled but body size is missing; set size_x/size_y/size_z')
    offset = tuple(float(value(f'self_filter_offset_{axis}')) for axis in 'xyz')
    padding = validate_number(value('self_filter_padding'), 'self_filter_padding', allow_zero=True)
    return SelfFilterSettings(enabled, frame, size, offset, padding)


def robot_description(settings):
    sx, sy, sz = settings.size
    ox, oy, oz = settings.offset
    return (
        '<?xml version="1.0"?>'
        '<robot name="agt_self_filter">'
        f'<link name="{settings.frame}">'
        '<collision>'
        f'<origin xyz="{ox} {oy} {oz}" rpy="0 0 0"/>'
        f'<geometry><box size="{sx} {sy} {sz}"/></geometry>'
        '</collision>'
        '</link>'
        '</robot>'
    )


def self_filter_nodes(settings, use_sim_time):
    if not settings.enabled:
        return []
    frame = settings.frame
    return [
        Node(
            package='robot_self_filter', executable='self_filter', name='self_filter', output='screen',
            parameters=[{
                'use_sim_time': use_sim_time,
                'lidar_sensor_type': 0,
                'robot_description': robot_description(settings),
                'sensor_frame': frame,
                'in_pointcloud_topic': '/mapping/sensor/cloud_unfiltered',
                'keep_organized': True,
                'zero_for_removed_points': False,
                'invert': False,
                'min_sensor_dist': 0.01,
                'publish_collision_shapes': True,
                'default_box_scale': [1.0, 1.0, 1.0],
                'default_box_padding': [settings.padding, settings.padding, settings.padding],
                'self_see_links.names': [frame],
                f'self_see_links.{frame}.shadow': False,
            }],
            remappings=[('cloud_out', '/mapping/sensor/cloud_filtered')],
        ),
        Node(
            package='agt_livox_self_filter_bridge', executable='livox_self_filter_bridge_node',
            name='livox_self_filter_bridge_node', output='screen',
            parameters=[{'use_sim_time': use_sim_time}],
        ),
    ]


def adapter_parameters(input_topic, settings):
    params = {'input_topic': input_topic}
    if settings.enabled:
        params.update({
            'output_topic': '/mapping/sensor/cloud_unfiltered',
            'sanitized_topic': '/mapping/sensor/livox_unfiltered',
            'frame_id': settings.frame,
        })
    return params
