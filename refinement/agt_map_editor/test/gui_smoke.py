"""Display-required interaction smoke test; restores the real map and leaves editor open."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import tkinter as tk
import numpy as np
from PIL import Image, ImageGrab
import yaml
from agt_map_editor.grid_editor import GridEditor

root = tk.Tk()
editor = GridEditor(root)
root.update()
with TemporaryDirectory(prefix='agt-grid-gui-') as directory:
    path = Path(directory)
    pixels = np.full((100,100), 205, dtype=np.uint8)
    pixels[:5] = 0
    Image.fromarray(pixels).save(path/'map.pgm')
    (path/'map.yaml').write_text(yaml.safe_dump(dict(image='map.pgm', resolution=.1,
        origin=[-1.,-2.,.2], negate=0, occupied_thresh=.65, free_thresh=.196)))
    editor.load(path/'map.yaml')
    root.update()
    def event(x,y):
        sx,sy = editor.screen((x,y))
        return SimpleNamespace(x=sx, y=sy)
    def visible_color(column, row):
        editor.render()
        x, y = editor.screen((column+.5, row+.5))
        return editor.photo._PhotoImage__photo.get(round(x), round(y))

    # Selection alone cannot alter any pixel; actions modify and render the real state.
    editor.tool.set('矩形框')
    editor.press(event(10,0))
    editor.drag(event(20,10))
    editor.release(event(20,10))
    assert np.array_equal(editor.document.pixels, pixels)
    assert editor.selection is not None and editor.closed
    editor.preset('空闲', '全部格子')
    assert (editor.document.classes() == 1).sum() == 445
    assert np.all(editor.document.classes()[:11,10:21] == 0)
    assert visible_color(15,2) == (255,255,255)
    editor.preset('未知', '全部格子')
    assert np.all(editor.document.classes()[:11,10:21] == 2)
    assert visible_color(15,2) == (205,205,205)
    editor.preset('障碍', '全部格子')
    assert np.all(editor.document.classes()[:11,10:21] == 1)
    assert visible_color(15,2) == (0,0,0)
    exported = editor.document.export(path/'edited')
    from agt_map_editor.grid_document import GridDocument
    assert np.array_equal(GridDocument(exported).pixels, editor.document.pixels)
    editor.undo()
    editor.undo()
    editor.undo()
    assert np.array_equal(editor.document.pixels, pixels)
    editor.tool.set('多边形框')
    for point in [(20,20),(40,20),(30,40)]:
        editor.press(event(*point))
    editor.finish_polygon()
    assert np.array_equal(editor.document.pixels, pixels)
    editor.preset('空闲', '全部格子')
    assert (editor.document.classes() == 0).sum() > 100
    assert visible_color(30,25) == (255,255,255)
    editor.undo()
    editor.tool.set('自由画框')
    editor.press(event(30,30))
    for point in [(60,30),(60,60),(30,60)]:
        editor.drag(event(*point))
    editor.release(event(30,30))
    assert np.array_equal(editor.document.pixels, pixels)
    editor.preset('空闲', '全部格子')
    assert (editor.document.classes() == 0).sum() > 500
    assert visible_color(45,45) == (255,255,255)
    editor.undo()
    editor.tool.set('矩形框')
    editor.press(event(20,20))
    editor.release(event(40,40))
    assert np.array_equal(editor.document.pixels, pixels)
    editor.preset('障碍', '全部格子')
    assert np.all(editor.document.classes()[20:41,20:41] == 1)
    assert visible_color(30,30) == (0,0,0)
    editor.target.set('未知')
    editor.target_changed()
    assert np.all(editor.document.classes()[20:41,20:41] == 2)
    assert visible_color(30,30) == (205,205,205)
    editor.undo()
    editor.undo()
    assert np.array_equal(editor.document.pixels, pixels)
    anchor = event(40,40)
    old = editor.position(anchor)
    editor.zoom(anchor, 1.2)
    assert np.allclose(editor.position(anchor), old)
    editor.pan_start(SimpleNamespace(x=100,y=100))
    editor.pan(SimpleNamespace(x=120,y=130))
    assert np.allclose(editor.position(SimpleNamespace(x=anchor.x+20,y=anchor.y+30)), old)
    editor.document.dirty = False
editor.tool.set('多边形框')
editor.target.set('空闲')
editor.source.set('全部格子')
editor.load(sys.argv[1])
root.update()
def capture():
    box = (root.winfo_rootx(), root.winfo_rooty(),
           root.winfo_rootx()+root.winfo_width(), root.winfo_rooty()+root.winfo_height())
    ImageGrab.grab(bbox=box).save('/tmp/agt-grid-editor-preview.png')
root.after(1200, capture)
print('GUI smoke passed: selection alone preserves data; action buttons alter pixels; visible colors are white/grey/black; exported pixels match; undo/redo and pan/zoom pass.', flush=True)
if '--test-only' in sys.argv:
    root.destroy()
else:
    root.mainloop()
