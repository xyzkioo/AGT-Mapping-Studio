"""Standalone Tk editor for Nav2 trinary occupancy maps (no ROS runtime required)."""
import argparse
import math
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import numpy as np
from PIL import Image, ImageDraw, ImageTk
from .grid_document import GridDocument


class GridEditor:
    def __init__(self, root, filename=None):
        self.root = root
        root.title('AGT 二维地图手动编辑 · 简化版')
        root.geometry('1280x850')
        self.document = None
        self.scale, self.offset_x, self.offset_y = 1., 0., 0.
        self.points, self.start = [], None
        self.selection = None
        self.closed = False
        self.tool = tk.StringVar(value='多边形框')
        self.target = tk.StringVar(value='空闲')
        self.source = tk.StringVar(value='全部格子')
        self.selection_info = tk.StringVar(value='先画框选中区域，再点击具体操作；框内显示真实黑白灰。')
        self.reason = tk.StringVar()
        self.status = tk.StringVar(value='打开 Nav2 地图 YAML 开始编辑。')
        bar = ttk.Frame(root, padding=6)
        bar.pack(fill='x')
        for label, command in [('打开地图', self.open_dialog), ('另存新目录', self.save),
                               ('撤销 Ctrl+Z', self.undo), ('重做 Ctrl+Y', self.redo), ('适应窗口', self.fit)]:
            ttk.Button(bar, text=label, command=command).pack(side='left', padx=3)
        controls = ttk.Frame(root, padding=6)
        controls.pack(fill='x')
        ttk.Label(controls, text='工具').pack(side='left', padx=4)
        combo = ttk.Combobox(controls, textvariable=self.tool,
                             values=['多边形框', '自由画框', '矩形框'],
                             state='readonly', width=10)
        combo.pack(side='left')
        combo.bind('<<ComboboxSelected>>', lambda e: self.cancel())
        ttk.Button(controls, text='闭合画框 Enter', command=self.finish_polygon).pack(side='left', padx=5)
        actions = ttk.Frame(root, padding=6)
        actions.pack(fill='x')
        for text, target in [('设为空闲', '空闲'), ('设为未知', '未知'), ('设为障碍', '障碍')]:
            ttk.Button(actions, text=text,
                       command=lambda t=target: self.preset(t, '全部格子')).pack(side='left', padx=3)
        ttk.Button(actions, text='取消画框 Esc', command=self.cancel).pack(side='left')
        ttk.Label(root, textvariable=self.selection_info, padding=5).pack(fill='x')
        row = ttk.Frame(root, padding=6)
        row.pack(fill='x')
        ttk.Label(row, text='修改依据/备注：').pack(side='left')
        ttk.Entry(row, textvariable=self.reason).pack(side='left', fill='x', expand=True)
        ttk.Label(root, text='黑=障碍 灰=未知 白=空闲；亮蓝边框=选区｜画框只选中，点击操作立即修改；Ctrl+Z 撤销', padding=5).pack(fill='x')
        self.canvas = tk.Canvas(root, bg='#343b45', highlightthickness=0)
        self.canvas.pack(fill='both', expand=True)
        ttk.Label(root, textvariable=self.status, padding=6).pack(fill='x')
        self.canvas.bind('<ButtonPress-1>', self.press)
        self.canvas.bind('<B1-Motion>', self.drag)
        self.canvas.bind('<ButtonRelease-1>', self.release)
        self.canvas.bind('<Motion>', self.hover)
        self.canvas.bind('<ButtonPress-3>', self.pan_start)
        self.canvas.bind('<B3-Motion>', self.pan)
        self.canvas.bind('<MouseWheel>', lambda e: self.zoom(e, 1.2 if e.delta > 0 else 1/1.2))
        self.canvas.bind('<Button-4>', lambda e: self.zoom(e, 1.2))
        self.canvas.bind('<Button-5>', lambda e: self.zoom(e, 1/1.2))
        self.canvas.bind('<Configure>', lambda e: self.render())
        root.bind('<Control-z>', lambda e: self.undo())
        root.bind('<Control-y>', lambda e: self.redo())
        root.bind('<Control-s>', lambda e: self.save())
        root.bind('<Escape>', lambda e: self.cancel())
        self.canvas.bind('<Return>', lambda e: self.finish_polygon())
        root.protocol('WM_DELETE_WINDOW', self.close)
        if filename:
            self.load(filename)

    def can_discard(self):
        return not self.document or not self.document.dirty or messagebox.askyesno('未导出修改', '有尚未导出的修改，确定放弃？')

    def close(self):
        if self.can_discard():
            self.root.destroy()

    def open_dialog(self):
        if not self.can_discard():
            return
        filename = filedialog.askopenfilename(filetypes=[('地图 YAML', '*.yaml *.yml')])
        if filename:
            self.load(filename)

    def load(self, filename):
        try:
            document = GridDocument(filename)
        except Exception as exc:
            messagebox.showerror('无法打开地图', str(exc))
            return
        self.document = document
        self.cancel()
        self.root.title('AGT 二维地图手动编辑 · 简化版 — ' + str(document.path))
        self.root.update_idletasks()
        self.fit()
        h, w = document.pixels.shape
        self.status.set(f'已打开 {w}×{h} 格；{document.resolution:g} m/格；原点 {document.origin}。画框只选中；点击具体操作即可修改。')

    def fit(self):
        if not self.document:
            return
        h, w = self.document.pixels.shape
        cw, ch = self.canvas.winfo_width(), self.canvas.winfo_height()
        self.scale = max(.02, min((cw-20)/w, (ch-20)/h))
        self.offset_x, self.offset_y = (cw-w*self.scale)/2, (ch-h*self.scale)/2
        self.render()

    def position(self, event):
        return ((event.x-self.offset_x)/self.scale, (event.y-self.offset_y)/self.scale)

    def screen(self, p):
        return p[0]*self.scale+self.offset_x, p[1]*self.scale+self.offset_y

    def render(self):
        if not self.document:
            return
        # Render only the viewport, keeping memory bounded when zoomed into a large map.
        cw, ch = max(1, self.canvas.winfo_width()), max(1, self.canvas.winfo_height())
        palette = np.array([[255,255,255], [0,0,0], [205,205,205]], dtype=np.uint8)
        rgb = palette[self.document.classes()]
        im = Image.fromarray(rgb)
        box = (-self.offset_x/self.scale, -self.offset_y/self.scale,
               (cw-self.offset_x)/self.scale, (ch-self.offset_y)/self.scale)
        im = im.transform((cw, ch), getattr(Image, 'Transform', Image).EXTENT, box, resample=getattr(Image, 'Resampling', Image).NEAREST, fillcolor=(52,59,69))
        self.photo = ImageTk.PhotoImage(im)
        self.canvas.delete('map')
        self.canvas.create_image(0, 0, anchor='nw', image=self.photo, tags='map')
        self.canvas.tag_lower('map')
        self.draw_selection()
        self.update_selection_info()

    def target_changed(self, event=None):
        # A newly chosen target applies to every state unless the user explicitly filters it.
        self.source.set('全部格子')
        self.apply_selection()
        self.render()

    def preset(self, target, source):
        self.target.set(target)
        self.source.set(source)
        self.apply_selection()
        self.render()

    def filter_changed(self):
        self.apply_selection()
        self.render()

    def update_selection_info(self):
        if self.selection is None:
            self.selection_info.set(f'先画框选中区域，再点击设为空闲、设为未知或设为障碍。')
            return
        mask = np.asarray(self.selection, dtype=bool)
        classes = self.document.classes()
        counts = [int(np.count_nonzero(mask & (classes == i))) for i in range(3)]
        self.selection_info.set(f'框内 {int(mask.sum())} 格：黑色障碍 {counts[1]} / 灰色未知 {counts[2]} / 白色空闲 {counts[0]} ｜点击状态按钮修改整个选区')

    def draw_selection(self):
        self.canvas.delete('selection')
        if self.points:
            display_points = self.points + [self.points[0]] if self.closed else self.points
            coords = [v for p in display_points for v in self.screen(p)]
            if len(coords) >= 4:
                self.canvas.create_line(*coords, fill='#25c8ef', width=2, tags='selection')
            # Vertex markers describe the selected boundary.
            if self.tool.get() != '自由画框':
                for p in self.points:
                    x, y = self.screen(p)
                    self.canvas.create_oval(x-5,y-5,x+5,y+5, fill='#25c8ef', outline='#08607f', tags='selection')
        if self.start and self.tool.get() == '矩形框':
            self.canvas.create_rectangle(*self.screen(self.start), *self.screen(self.end), outline='#25c8ef', width=2, tags='selection')

    def cancel(self):
        self.points, self.start = [], None
        self.selection = None
        self.closed = False
        self.canvas.delete('selection')
        if self.document:
            self.render()
        else:
            self.selection_info.set('先画框选中区域，再点击具体操作。')

    def press(self, event):
        if not self.document:
            return
        self.canvas.focus_set()
        if self.closed:
            self.cancel()
        point = self.position(event)
        if self.tool.get() == '多边形框':
            self.points.append(point)
        else:
            self.start = self.end = point
            self.points = [point] if self.tool.get() == '自由画框' else []
        self.draw_selection()

    def new_mask(self):
        h, w = self.document.pixels.shape
        return Image.new('1', (w, h))

    def drag(self, event):
        if self.start:
            point = self.position(event)
            if self.tool.get() == '自由画框':
                previous = self.points[-1]
                if math.hypot(point[0]-previous[0], point[1]-previous[1])*self.scale >= 2:
                    self.points.append(point)
            self.end = point
            self.draw_selection()

    def release(self, event):
        if not self.start:
            return
        if self.tool.get() == '自由画框':
            self.points.append(self.position(event))
        else:
            a, b = self.start, self.position(event)
            left, right = sorted([a[0], b[0]])
            top, bottom = sorted([a[1], b[1]])
            self.points = [(left,top), (right,top), (right,bottom), (left,bottom)]
        self.start = None
        self.finish_polygon()

    def build_selection(self):
        mask = self.new_mask()
        ImageDraw.Draw(mask).polygon(self.points, fill=1)
        self.selection = mask
        self.closed = True
        self.render()

    def finish_polygon(self):
        if not self.document:
            return
        if len(self.points) < 3:
            self.status.set('画框至少需要三个顶点；多边形逐点点击，自由画框按住左键描边。')
            return
        self.build_selection()
        self.status.set('区域已选中，地图尚未修改。点击设为空闲、设为未知或设为障碍即可立即修改。')

    def apply_selection(self):
        if self.selection is None:
            return
        count = self.document.apply(np.asarray(self.selection, dtype=bool), self.target.get(), self.source.get(),
                                    self.reason.get().strip(), self.tool.get())
        self.render()
        self.status.set(f'已修改 {count} 格 → {self.target.get()}；蓝色框为选区。Ctrl+Z 可撤销。')

    def undo(self):
        if self.document:
            self.cancel()
            self.document.undo()
            self.render()
            self.status.set('已撤销（若无历史则保持不变）。')

    def redo(self):
        if self.document:
            self.cancel()
            self.document.redo()
            self.render()
            self.status.set('已重做（若无历史则保持不变）。')

    def pan_start(self, event):
        self.pan_anchor = event.x, event.y

    def pan(self, event):
        if not hasattr(self, 'pan_anchor'):
            return
        x, y = self.pan_anchor
        self.offset_x += event.x-x
        self.offset_y += event.y-y
        self.pan_anchor = event.x, event.y
        self.render()

    def zoom(self, event, factor):
        if not self.document:
            return
        px, py = self.position(event)
        self.scale = min(40., max(.02, self.scale*factor))
        self.offset_x, self.offset_y = event.x-px*self.scale, event.y-py*self.scale
        self.render()

    def hover(self, event):
        if not self.document or self.start or self.selection is not None:
            return
        x, y = self.position(event)
        col, row = math.floor(x), math.floor(y)
        h, w = self.document.pixels.shape
        if 0 <= col < w and 0 <= row < h:
            wx, wy = self.document.world(col, row)
            probability = self.document.probability(self.document.pixels[row,col])
            state = '障碍' if probability > self.document.metadata['occupied_thresh'] else '空闲' if probability < self.document.metadata['free_thresh'] else '未知'
            self.status.set(f'地图坐标 ({wx:.2f}, {wy:.2f}) m | 像素 ({col}, {row}) | {state} | 显示实际占据状态')

    def save(self):
        if not self.document:
            return
        if (self.points and not self.closed) or self.start:
            messagebox.showinfo('选择尚未完成', '画框尚未闭合。请先闭合画框或按 Esc 取消，然后另存。')
            return
        filename = filedialog.asksaveasfilename(title='填写新的输出目录名（会创建目录，不覆盖已有目录）',
                                              initialdir=str(self.document.path.parent), initialfile='manually_edited_map')
        if not filename:
            return
        try:
            result = self.document.export(filename)
        except Exception as exc:
            messagebox.showerror('导出失败', str(exc))
            return
        self.status.set('已导出：' + str(result))
        messagebox.showinfo('导出完成', f'{result}\n另含 edits.json 和 changed_cells.png。\n原地图与点云保持不变。')


def main():
    parser = argparse.ArgumentParser(description='AGT Nav2 二维地图手动编辑器')
    parser.add_argument('map_yaml', nargs='?', help='Nav2 trinary 地图 YAML 路径')
    args = parser.parse_args()
    root = tk.Tk()
    GridEditor(root, args.map_yaml)
    root.mainloop()


if __name__ == '__main__':
    main()
