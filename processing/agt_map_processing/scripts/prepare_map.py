#!/usr/bin/env python3
"""Move an existing processing profile into a map data package without copying scans."""
import argparse
import json
import os
from pathlib import Path


def prepare(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    data = json.loads(source.read_text())
    profile = {key: data[key] for key in ('schema_version', 'name', 'package', 'assets', 'coordinate_system') if key in data}
    def relative(value):
        path = Path(value)
        if not path.is_absolute():
            path = source.parent / path
        return os.path.relpath(path.resolve(), destination.parent)
    profile['schema_version'] = 1
    profile['package'] = relative(profile['package'])
    profile['assets'] = {key: relative(value) for key, value in profile['assets'].items()}
    profile.pop('sha256', None)
    # Exclusive creation: never replace an existing data configuration.
    with destination.open('x') as stream:
        json.dump(profile, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    return destination


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.profile, args.output))
