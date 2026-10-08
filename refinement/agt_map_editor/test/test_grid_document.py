import json
import math
import numpy as np
import pytest
import yaml
from PIL import Image
from agt_map_editor.grid_document import GridDocument


def make_map(tmp_path, negate=0, yaw=0):
    pixels = np.array([[0, 205, 255], [255, 205, 0]], dtype=np.uint8)
    if negate:
        pixels = 255-pixels
    Image.fromarray(pixels).save(tmp_path/'source.pgm')
    meta = dict(image='source.pgm', mode='trinary', resolution=.1,
                origin=[-90.9,-45.7,yaw], negate=negate, occupied_thresh=.65, free_thresh=.196)
    (tmp_path/'source.yaml').write_text(yaml.safe_dump(meta))
    return GridDocument(tmp_path/'source.yaml')


@pytest.mark.parametrize('negate', [0,1])
def test_filtered_edit_undo_redo_export_preserves_geometry(tmp_path, negate):
    doc = make_map(tmp_path, negate)
    original_bytes = (tmp_path/'source.pgm').read_bytes()
    assert doc.apply(np.ones(doc.pixels.shape, bool), '空闲', '仅未知', '现场确认') == 2
    np.testing.assert_array_equal(doc.classes(), [[1,0,0],[0,0,1]])
    doc.undo()
    np.testing.assert_array_equal(doc.pixels, doc.original)
    doc.redo()
    result = doc.export(tmp_path/'edited')
    loaded = GridDocument(result)
    np.testing.assert_array_equal(loaded.pixels, doc.pixels)
    assert loaded.origin == doc.origin and loaded.resolution == doc.resolution
    assert (tmp_path/'source.pgm').read_bytes() == original_bytes
    report = json.loads((result.parent/'edits.json').read_text())
    assert report['changed_cells'] == 2
    assert report['operations'][0]['reason'] == '现场确认'
    with pytest.raises(FileExistsError):
        doc.export(tmp_path/'edited')
    with pytest.raises(FileExistsError):
        doc.export(tmp_path)


def test_world_coordinates_with_rotation(tmp_path):
    doc = make_map(tmp_path, yaw=math.pi/2)
    x,y = doc.world(0,0)
    assert x == pytest.approx(-91.05)
    assert y == pytest.approx(-45.65)


def test_branch_history_and_obstacle_filter(tmp_path):
    doc = make_map(tmp_path)
    mask = np.ones(doc.pixels.shape, bool)
    doc.apply(mask, '空闲', '仅未知')
    doc.undo()
    assert doc.apply(mask, '未知', '仅障碍') == 2
    doc.redo()
    assert len(doc.operations) == 1
    np.testing.assert_array_equal(doc.classes(), [[2,2,0],[0,2,2]])


def test_unsupported_mode_does_not_load(tmp_path):
    doc = make_map(tmp_path)
    meta = doc.metadata.copy()
    meta['mode'] = 'raw'
    doc.path.write_text(yaml.safe_dump(meta))
    with pytest.raises(ValueError, match='trinary'):
        GridDocument(doc.path)
