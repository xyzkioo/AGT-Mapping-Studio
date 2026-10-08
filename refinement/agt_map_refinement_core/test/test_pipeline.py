import hashlib
from pathlib import Path

import yaml

from agt_map_refinement_core.pipeline import refine_map_package


PCD = """# .PCD v0.7 - Point Cloud Data file format
VERSION 0.7
FIELDS x y z intensity
SIZE 4 4 4 4
TYPE F F F F
COUNT 1 1 1 1
WIDTH 4
HEIGHT 1
VIEWPOINT 0 0 0 1 0 0 0
POINTS 4
DATA ascii
0 0 0 1
1 1 0 1
2 2 0 1
4 4 0 1
"""


def make_source(tmp_path: Path) -> Path:
    source = tmp_path / 'source'
    (source / 'patches').mkdir(parents=True)
    (source / 'map.pcd').write_text(PCD)
    (source / 'poses.txt').write_text('0.pcd 0 0 0 1 0 0 0\n')
    (source / 'poses_timed.txt').write_text('0.pcd 1.0 0 0 0 1 0 0 0\n')
    (source / 'patches' / '0.pcd').write_text(PCD)
    (source / 'metadata.yaml').write_text('backend: PGO\n')
    (source / 'manifest.yaml').write_text('schema_version: 1\n')
    (source / 'checksums.sha256').write_text('')
    return source


def run(tmp_path, rules):
    rules_path = tmp_path / 'refinement.yaml'
    rules_path.write_text(yaml.safe_dump({'version': 1, 'operations': rules}))
    return refine_map_package(make_source(tmp_path), rules_path, tmp_path / 'refined')


def test_empty_rules_preserve_points_and_publish_derivatives(tmp_path):
    result = run(tmp_path, [])
    assert (result / 'map.pcd').read_text() == PCD
    assert (result / 'nav_map.pgm').read_bytes().startswith(b'P5\n')
    assert (result / 'nav_map.yaml').is_file()
    assert yaml.safe_load((result / 'filter_report.yaml').read_text())['removed_by_rules'] == 0
    manifest = yaml.safe_load((result / 'manifest.yaml').read_text())
    assert manifest['package_kind'] == 'refined_mapping_source'
    checksums = {name: digest for digest, name in
                 (line.split('  ', 1) for line in (result / 'checksums.sha256').read_text().splitlines())}
    for name, digest in checksums.items():
        assert hashlib.sha256((result / name).read_bytes()).hexdigest() == digest
        assert manifest['checksums'][name] == digest


def test_polygon_and_box_remove_points(tmp_path):
    result = run(tmp_path, [{
        'type': 'remove_polygon', 'points': [[0.5, 0.5], [1.5, 0.5], [1.5, 1.5], [0.5, 1.5]],
    }, {
        'type': 'remove_box', 'min': {'x': 1.9, 'y': 1.9, 'z': -1},
        'max': {'x': 2.1, 'y': 2.1, 'z': 1},
    }])
    assert '1 1 0 1' not in (result / 'map.pcd').read_text()
    assert '2 2 0 1' not in (result / 'map.pcd').read_text()
    assert '4 4 0 1' in (result / 'map.pcd').read_text()


def test_forbidden_zone_is_exported_to_nav_map(tmp_path):
    result = run(tmp_path, [{
        'type': 'forbidden_zone', 'polygon': [[3, 3], [5, 3], [5, 5], [3, 5]],
    }])
    assert '4 4 0 1' in (result / 'map.pcd').read_text()
    assert 100 in (result / 'nav_map.pgm').read_bytes()[3:]

def test_exact_indices_delete_only_selected_rows_and_reject_changed_source(tmp_path):
    import pytest
    from agt_map_refinement_core.pcd import read_pcd
    source = make_source(tmp_path)
    # Duplicate coordinates and a nonfinite row must retain their original row IDs.
    pcd = PCD.replace('1 1 0 1', 'nan 0 0 1').replace('2 2 0 1', '0 0 0 1')
    (source / 'map.pcd').write_text(pcd)
    rules = tmp_path / 'exact.yaml'
    operation = {'type': 'remove_indices', 'indices': [0, 3], 'source_point_count': 4,
                 'source_sha256': hashlib.sha256((source / 'map.pcd').read_bytes()).hexdigest()}
    rules.write_text(yaml.safe_dump({'version': 1, 'operations': [operation]}))
    output = refine_map_package(source, rules, tmp_path / 'exact', write_preview_nav_map=False)
    rows = read_pcd(output / 'map.pcd').rows
    assert len(rows) == 2 and rows[0][0] == 'nan' and rows[1] == ['0', '0', '0', '1']
    (source / 'map.pcd').write_text(PCD)
    with pytest.raises(ValueError, match='do not match'):
        refine_map_package(source, rules, tmp_path / 'wrong')
    assert not (tmp_path / 'wrong').exists()


def test_exact_rules_reject_invalid_indices(tmp_path):
    import pytest
    from agt_map_refinement_core.rules import load_rules
    path = tmp_path / 'invalid.yaml'
    for invalid in [-1, 4, True, 1.5]:
        path.write_text(yaml.safe_dump({'version': 1, 'operations': [{
            'type': 'remove_indices', 'indices': [invalid], 'source_point_count': 4,
            'source_sha256': 'a' * 64}]}))
        with pytest.raises(ValueError, match='invalid point indices'):
            load_rules(path)
