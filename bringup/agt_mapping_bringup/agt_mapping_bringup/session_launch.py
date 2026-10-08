"""Launch composition and explicit lifecycle for one offline mapping session."""
import atexit
from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch.actions import EmitEvent, ExecuteProcess, LogInfo, OpaqueFunction, RegisterEventHandler
from launch.event_handlers import OnProcessExit, OnShutdown
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

from .preflight import inspect_bag, parse_bool, validate_number
from .session_state import create_session, mark_session, playback_action
from .session_lock import acquire_domain_lease
from .mapping_nodes import mapping_processes, mapping_rviz
from .self_filter_launch import (
    VEHICLE_RETURN_TOPIC, read_self_filter_settings, read_vehicle_return_settings)


def _raise_launch_error(_context, message):
    # Humble has no launch.actions.RaiseError. OpaqueFunction propagates this
    # exception through LaunchService, preserving a nonzero launch result.
    raise RuntimeError(message)


def launch_session(context):
    def value(name):
        return LaunchConfiguration(name).perform(context)

    bag = inspect_bag(value('bag_path'), value('lidar_topic'), value('imu_topic'))
    output = Path(value('output_dir')).expanduser().resolve()
    options = {
        'playback_rate': validate_number(value('playback_rate'), 'playback_rate'),
        'startup_timeout': validate_number(value('startup_timeout'), 'startup_timeout'),
        'export_timeout': validate_number(value('export_timeout'), 'export_timeout'),
        'drain_seconds': validate_number(value('drain_seconds'), 'drain_seconds', allow_zero=True),
        **{key: parse_bool(value(key)) for key in
           ('start_rviz', 'start_paused', 'auto_export', 'keep_open')},
    }
    if options['export_timeout'] <= options['drain_seconds']:
        raise ValueError('export_timeout must exceed drain_seconds')
    share = Path(get_package_share_directory('agt_mapping_bringup'))
    filter_settings = read_self_filter_settings(context)
    vehicle_settings = read_vehicle_return_settings(context)
    text = lambda value: ParameterValue(str(value), value_type=str)
    processes = mapping_processes(share, output, bag, True,
                                  filter_settings, vehicle_settings)
    playback_cmd = ['ros2', 'bag', 'play', str(bag.path), '--clock', '--rate',
                    str(options['playback_rate']), '--topics', bag.lidar_topic, bag.imu_topic]
    if options['start_paused']:
        playback_cmd.append('--start-paused')
    player = ExecuteProcess(cmd=playback_cmd, output='screen')
    ready = Node(package='agt_mapping_bringup', executable='mapping_wait_ready', output='screen',
                 parameters=[{'output_dir': text(output), 'startup_timeout': options['startup_timeout'],
                              'lidar_topic': bag.lidar_topic, 'imu_topic': bag.imu_topic,
                              'lidar_prefilter_topic': VEHICLE_RETURN_TOPIC if vehicle_settings.enabled else ''}])
    finalizer = Node(package='agt_mapping_bringup', executable='mapping_export_verified', output='screen',
                     parameters=[{'output_dir': text(output), 'export_timeout': options['export_timeout'],
                                  'drain_seconds': options['drain_seconds']}])

    def fail(message):
        mark_session(output, 'failed', message)
        return [OpaqueFunction(function=_raise_launch_error, args=[message])]

    def readiness_exit(event, ctx):
        if ctx.is_shutdown:
            return []
        if event.returncode != 0:
            return fail('Mapping readiness failed; bag was not started (see session.json/logs)')
        mark_session(output, 'replaying', 'Playing only the selected recorded lidar and IMU topics')
        message = '[2/4] Replaying bag. Ctrl+C cancels WITHOUT exporting a partial map.'
        if options['start_paused']:
            message += (' Player starts paused. Resume in the SAME ROS domain: '
                        'ros2 service call /rosbag2_player/resume rosbag2_interfaces/srv/Resume "{}"')
        return [LogInfo(msg=message), player]

    def playback_exit(event, ctx):
        action = playback_action(event.returncode, ctx.is_shutdown, options['auto_export'])
        if action == 'stop':
            return []
        if action == 'fail':
            return fail(f'Rosbag failed with exit code {event.returncode}; automatic export skipped')
        if action == 'manual':
            mark_session(output, 'waiting_manual_export', 'Playback finished; automatic export disabled')
            return [LogInfo(msg='Manual mode: request /mapping/backend/export_artifact, then run '
                                'scripts/verify_map_artifact.sh on the output. Ctrl+C closes nodes; '
                                'this mode does not declare automatic completion.')]
        return [finalizer]

    def export_exit(event, ctx):
        if ctx.is_shutdown:
            return []
        if event.returncode != 0:
            return fail('Map export/verification failed; no verified completion (see session.json/logs)')
        if options['keep_open']:
            return [LogInfo(msg=f'Verified map: {output / "map_package"}. --keep-open: Ctrl+C to close.')]
        return [LogInfo(msg=f'Verified map: {output / "map_package"}; closing this launch.'),
                EmitEvent(event=Shutdown(reason='Optimized map artifact verified'))]

    def critical_exit(label):
        def handler(event, ctx):
            if ctx.is_shutdown:
                return []
            return fail(f'{label} exited unexpectedly ({event.returncode}); stopping this session')
        return handler

    def shutdown(event, ctx):
        mark_session(output, 'cancelled', 'Launch stopped before automatic verified completion')
        return []

    # Register before starting processes, so even a fast startup failure is observed.
    actions = [
        RegisterEventHandler(OnShutdown(on_shutdown=shutdown)),
        RegisterEventHandler(OnProcessExit(target_action=ready, on_exit=readiness_exit)),
        RegisterEventHandler(OnProcessExit(target_action=player, on_exit=playback_exit)),
        RegisterEventHandler(OnProcessExit(target_action=finalizer, on_exit=export_exit)),
    ]
    actions.extend(RegisterEventHandler(OnProcessExit(target_action=node, on_exit=critical_exit(label)))
                   for node, label in processes)
    actions.extend(node for node, _ in processes)
    if options['start_rviz']:
        actions.append(mapping_rviz(share, True))
    actions.append(ready)
    lease = acquire_domain_lease()
    try:
        create_session(output, bag, options)
    except Exception:
        lease.close()
        raise
    # Keep the lock until ros2 launch exits, including its child-process teardown.
    atexit.register(lease.close)
    return actions
