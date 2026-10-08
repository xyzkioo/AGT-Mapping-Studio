"""Lossless source pixels, trinary edits, and separately exported Nav2 maps."""
from pathlib import Path
import hashlib
import json
import math
from datetime import datetime, timezone

import numpy as np
from PIL import Image
import yaml


class GridDocument:
    HISTORY_BYTES = 128 * 1024 * 1024

    def __init__(self, filename):
        self.path = Path(filename).resolve()
        self.metadata = yaml.safe_load(self.path.read_text())
        m = self.metadata
        if not isinstance(m, dict) or m.get('mode', 'trinary') != 'trinary':
            raise ValueError('仅支持 Nav2 trinary 地图；scale/raw 模式不能直接编辑。')
        self.resolution = float(m['resolution'])
        self.origin = tuple(float(x) for x in m['origin'])
        free, occupied = float(m['free_thresh']), float(m['occupied_thresh'])
        if (not math.isfinite(self.resolution) or self.resolution <= 0 or
                len(self.origin) != 3 or not all(math.isfinite(x) for x in self.origin) or
                not 0 < free < occupied < 1 or m.get('negate', 0) not in (0, 1)):
            raise ValueError('分辨率、原点或占据阈值无效。')
        self.image_path = (self.path.parent / m['image']).resolve()
        with Image.open(self.image_path) as im:
            if im.mode != 'L':
                raise ValueError('仅支持无透明通道的 8 位灰度地图。')
            self.pixels = np.array(im, dtype=np.uint8)
        self.original = self.pixels.copy()
        self.source_hash = hashlib.sha256(self.image_path.read_bytes()).hexdigest()
        self.yaml_hash = hashlib.sha256(self.path.read_bytes()).hexdigest()
        self.undo_stack, self.redo_stack = [], []
        self.operations = []
        self.dirty = False
        self.values = {'空闲': 0 if m.get('negate', 0) else 255,
                       '障碍': 255 if m.get('negate', 0) else 0}
        unknown = [i for i in range(256) if free <= self.probability(i) <= occupied]
        if not unknown:
            raise ValueError('此阈值没有可表示未知状态的灰度值。')
        preferred = 50 if m.get('negate', 0) else 205
        self.values['未知'] = preferred if preferred in unknown else min(unknown, key=lambda i: abs(self.probability(i) - (free + occupied) / 2))

    def probability(self, pixels):
        value = np.asarray(pixels, dtype=float) / 255.0
        return value if self.metadata.get('negate', 0) else 1 - value

    def classes(self):
        probability = self.probability(self.pixels)
        return np.where(probability > float(self.metadata['occupied_thresh']), 1,
                        np.where(probability < float(self.metadata['free_thresh']), 0, 2))

    def world(self, column, row):
        h = self.pixels.shape[0]
        x, y = (column + .5) * self.resolution, (h - row - .5) * self.resolution
        ox, oy, yaw = self.origin
        return ox + math.cos(yaw)*x - math.sin(yaw)*y, oy + math.sin(yaw)*x + math.cos(yaw)*y

    def apply(self, mask, target, source='仅未知', reason='', tool='选择'):
        if mask.shape != self.pixels.shape:
            raise ValueError('选择区域与地图尺寸不匹配。')
        eligible = np.asarray(mask, dtype=bool).copy()
        if source != '全部格子':
            eligible &= self.classes() == {'仅空闲': 0, '仅障碍': 1, '仅未知': 2}[source]
        value = self.values[target]
        eligible &= self.pixels != value
        indices = np.flatnonzero(eligible)
        if not indices.size:
            return 0
        before = self.pixels.ravel()[indices].copy()
        rows, columns = np.unravel_index(indices, self.pixels.shape)
        op = {'time_utc': datetime.now(timezone.utc).isoformat(), 'tool': tool,
              'target': target, 'source_filter': source, 'reason': reason,
              'cells': int(indices.size), 'pixel_bbox': [int(columns.min()), int(rows.min()),
                                                        int(columns.max()), int(rows.max())]}
        self.pixels.ravel()[indices] = value
        self.undo_stack.append((indices, before, value, op))
        self.redo_stack.clear()
        self.operations.append(op)
        while len(self.undo_stack) > 1 and sum(x[0].nbytes + x[1].nbytes for x in self.undo_stack) > self.HISTORY_BYTES:
            self.undo_stack.pop(0)
        self.dirty = True
        return int(indices.size)

    def undo(self):
        if self.undo_stack:
            record = self.undo_stack.pop()
            self.pixels.ravel()[record[0]] = record[1]
            self.redo_stack.append(record)
            self.operations.pop()
            self.dirty = True

    def redo(self):
        if self.redo_stack:
            record = self.redo_stack.pop()
            self.pixels.ravel()[record[0]] = record[2]
            self.undo_stack.append(record)
            self.operations.append(record[3])
            self.dirty = True

    def export(self, directory):
        """Exclusive new directory: source and previous exports cannot be overwritten."""
        output = Path(directory).resolve()
        output.mkdir(parents=True, exist_ok=False)
        # A failed export remains visibly incomplete and cannot be mistaken for a valid map.
        Image.fromarray(self.pixels).save(output / 'map.pgm')
        metadata = dict(self.metadata, image='map.pgm')
        (output / 'map.yaml').write_text(yaml.safe_dump(metadata, sort_keys=False), encoding='utf-8')
        changed = self.pixels != self.original
        delta = np.where(changed, 255, 0).astype(np.uint8)
        Image.fromarray(delta).save(output / 'changed_cells.png')
        report = {'source_yaml': str(self.path), 'source_image': str(self.image_path),
                  'source_image_sha256': self.source_hash, 'source_yaml_sha256': self.yaml_hash,
                  'output_image_sha256': hashlib.sha256((output / 'map.pgm').read_bytes()).hexdigest(),
                  'exported_utc': datetime.now(timezone.utc).isoformat(),
                  'changed_cells': int(changed.sum()), 'operations': self.operations,
                  'pixel_coordinates': 'column from left, row from top; world uses cell centers and origin yaw',
                  'note': '人工编辑二维地图；未修改点云；未验证实车导航。'}
        (output / 'edits.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        self.dirty = False
        return output / 'map.yaml'
