#!/usr/bin/env python3
"""Discover algorithm descriptors and execute them against map data packages."""
import argparse
import json
import os
from pathlib import Path
import signal
import string
import subprocess
import sys
import threading
import yaml

CHILD = None
CANCELLED = False


def discover(paths=None):
    if paths is None:
        paths = [Path(p) for p in os.environ.get('AGT_ALGORITHM_PATH', '').split(os.pathsep) if p]
        from ament_index_python.packages import get_packages_with_prefixes
        paths += [Path(prefix)/'share'/name/'algorithms' for name, prefix in get_packages_with_prefixes().items()]
    algorithms = {}
    for folder in dict.fromkeys(paths):
        for filename in sorted(Path(folder).glob('*.yaml')):
            spec = yaml.safe_load(filename.read_text())
            if not isinstance(spec, dict) or spec.get('schema_version') != 1:
                raise ValueError(f'Invalid algorithm descriptor: {filename}')
            key = spec.get('id')
            if not isinstance(key, str) or not key or key in algorithms:
                raise ValueError(f'Missing or duplicate algorithm ID in {filename}: {key}')
            if not isinstance(spec.get('command'), list) or not all(isinstance(x,str) for x in spec['command']) or not spec['command']:
                raise ValueError(f'command must be a nonempty argument list: {filename}')
            if not isinstance(spec.get('outputs'), list) or not spec['outputs']:
                raise ValueError(f'No declared outputs: {filename}')
            spec['_descriptor'] = str(filename.resolve())
            algorithms[key] = spec
    return algorithms


def map_inputs(package, profile_path=None, point_cloud=None):
    package = Path(package).resolve()
    data = {'map_package':str(package), 'map_pcd':str(Path(point_cloud).resolve() if point_cloud else package/'map.pcd'),
            'poses':str(package/'poses.txt'), 'poses_timed':str(package/'poses_timed.txt'),
            'patches':str(package/'patches'), 'metadata':str(package/'metadata.yaml')}
    profile = Path(profile_path).resolve() if profile_path else package/'processing_profile.json'
    if profile.is_file():
        value = json.loads(profile.read_text())
        data['profile'] = str(profile)
        for key, path in value.get('assets', {}).items():
            if key in data:
                raise ValueError(f'Asset name is reserved: {key}')
            data[key] = str((profile.parent/path).resolve())
    return data


def missing_inputs(spec, data):
    return [key for key in spec.get('required_inputs', []) if key not in data or not Path(data[key]).exists()]


def input_metadata(filename):
    """Freshness metadata only; never compute input content hashes."""
    path = Path(filename)
    def entry(item, name):
        stat = item.stat()
        return {'path':name, 'size':stat.st_size, 'mtime_ms':stat.st_mtime_ns//1000000}
    if path.is_dir():
        return {'kind':'directory', 'entries':[
            entry(p,p.relative_to(path).as_posix()) for p in sorted(path.rglob('*')) if p.is_file()]}
    return {'kind':'file', **entry(path, '')}


def parameters(spec, overrides):
    definitions = spec.get('parameters', {})
    unknown = set(overrides)-set(definitions)
    if unknown:
        raise ValueError(f'Unknown algorithm parameters: {sorted(unknown)}')
    values = {}
    for key, definition in definitions.items():
        value = overrides.get(key, definition.get('default'))
        kind = definition.get('type', 'string')
        converters = {'float':float, 'int':int, 'string':str}
        if kind not in converters or value is None:
            raise ValueError(f'Invalid parameter definition: {key}')
        value = converters[kind](value)
        if kind in ('float','int'):
            import math
            if not math.isfinite(value) or ('min' in definition and value < definition['min']) or ('max' in definition and value > definition['max']):
                raise ValueError(f'Parameter out of range: {key}')
        values[key] = value
    return values


def substitute(text, values):
    # Simple named fields only: no object traversal, format specs or shell evaluation.
    for _, field, fmt, conversion in string.Formatter().parse(text):
        if field is not None and (field not in values or fmt or conversion):
            raise ValueError(f'Unknown or unsupported placeholder: {field}')
    return text.format_map(values)


def atomic_json(path, data):
    temp = path.with_suffix('.json.tmp')
    temp.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
    temp.replace(path)


def cancel(signum, frame):
    global CANCELLED
    CANCELLED = True
    process = CHILD
    if process is not None and process.poll() is None:
        try: os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError: return
        def kill():
            if process.poll() is None:
                try: os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError: pass
        timer=threading.Timer(2,kill); timer.daemon=True; timer.start()


def execute(spec, data, output, overrides=None, check_only=False):
    global CHILD, CANCELLED
    CANCELLED = False
    if set(spec.get('parameters', {})).intersection(set(data) | {'output','artifact_dir'}):
        raise ValueError('Parameter names must not override input or output bindings')
    missing = missing_inputs(spec,data)
    if missing:
        raise ValueError(f'Missing required data for {spec["id"]}: {", ".join(missing)}')
    params = parameters(spec,overrides or {})
    output = Path(output).resolve()
    bindings = {**data, **params, 'output':str(output), 'artifact_dir':str(output/'artifacts')}
    command = [substitute(token,bindings) for token in spec['command']]
    declared=[]
    for entry in spec['outputs']:
        path=Path(substitute(entry['path'],bindings)).resolve()
        if not path.is_relative_to(output):
            raise ValueError(f'Output must be inside job directory: {path}')
        declared.append({'type':entry['type'], 'path':str(path)})
    if check_only:
        print(json.dumps({'status':'validated','algorithm_id':spec['id'],'parameters':params,'command':command},ensure_ascii=False))
        return 0
    provenance = {'descriptor':spec['_descriptor'],
                  'descriptor_text':Path(spec['_descriptor']).read_text(),
                  'inputs':{key:input_metadata(data[key]) for key in spec.get('required_inputs', [])}}
    output.mkdir(parents=True,exist_ok=False)
    result={'schema_version':1,'algorithm_id':spec['id'],'status':'running','parameters':params,
            'inputs':data,'outputs':[],'error':None,'provenance':provenance,'command':command}
    atomic_json(output/'result.json',result)
    try:
        print(json.dumps({'event':'started','algorithm_id':spec['id']},ensure_ascii=False),flush=True)
        CHILD=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,start_new_session=True)
        with (output/'algorithm.log').open('w') as log:
            for line in CHILD.stdout:
                log.write(line); log.flush(); print(line,end='',flush=True)
        code=CHILD.wait()
        if CANCELLED:
            result['status']='cancelled'; result['error']='Processing cancelled'
        elif code:
            raise RuntimeError(f'Algorithm exited with code {code}; see algorithm.log')
        else:
            if provenance['inputs'] != {key:input_metadata(data[key]) for key in spec.get('required_inputs', [])}:
                raise ValueError('Input files changed during processing; rerun with stable inputs')
            if Path(spec['_descriptor']).exists() and Path(spec['_descriptor']).read_text() != provenance['descriptor_text']:
                raise ValueError('Algorithm registration changed during processing; rerun')
            for entry in declared:
                if not Path(entry['path']).resolve().is_relative_to(output):
                    raise ValueError(f'Output escapes job directory: {entry["path"]}')
                if not Path(entry['path']).exists():
                    raise ValueError(f'Algorithm did not produce declared output: {entry["path"]}')
            result['status']='complete'; result['outputs']=declared
    except Exception as error:
        result['status']='failed'; result['error']=str(error)
    finally:
        if CHILD is not None and CHILD.stdout is not None: CHILD.stdout.close()
        CHILD=None
        atomic_json(output/'result.json',result)
        # Self-describing map packages remain recognizable after reopening in Studio.
        for entry in result['outputs']:
            if entry['type']=='map_package':
                atomic_json(Path(entry['path'])/'result.json',result)
    print(json.dumps({'event':result['status'],'result':str(output/'result.json')},ensure_ascii=False),flush=True)
    return 0 if result['status']=='complete' else (130 if result['status']=='cancelled' else 1)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--list-json',action='store_true')
    parser.add_argument('--map-package','--package',dest='map_package',type=Path)
    parser.add_argument('--profile',type=Path)
    parser.add_argument('--point-cloud',type=Path)
    parser.add_argument('--algorithm')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--param',action='append',default=[],metavar='NAME=VALUE')
    parser.add_argument('--check-only',action='store_true')
    args=parser.parse_args()
    registry=discover()
    data=map_inputs(args.map_package,args.profile,args.point_cloud) if args.map_package else None
    if args.list_json:
        entries=[]
        for spec in registry.values():
            entry={key:spec.get(key) for key in ('id','name','required_inputs','parameters','outputs')}
            entry['missing_inputs']=missing_inputs(spec,data) if data is not None else []
            entry['available']=not entry['missing_inputs'];entries.append(entry)
        print(json.dumps({'schema_version':1,'algorithms':entries},ensure_ascii=False));return 0
    if data is None or args.output is None:
        parser.error('--map-package and --output are required')
    key=args.algorithm
    if key is None and len(registry)==1: key=next(iter(registry))
    if key not in registry:
        raise ValueError('Select a registered algorithm with --algorithm; use --list-json')
    overrides={}
    for setting in args.param:
        if '=' not in setting: parser.error('--param requires NAME=VALUE')
        name,value=setting.split('=',1)
        if name in overrides: parser.error(f'Duplicate parameter: {name}')
        overrides[name]=value
    return execute(registry[key],data,args.output,overrides,args.check_only)


if __name__=='__main__':
    signal.signal(signal.SIGTERM,cancel);signal.signal(signal.SIGINT,cancel)
    try: sys.exit(main())
    except Exception as error:
        print(f'Map runner failed: {error}',file=sys.stderr);sys.exit(1)
