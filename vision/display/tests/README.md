# display tests

`display_render_test` 测试 `display::render()`。目标直接编译 `render_test.cpp` 和 `render.cpp`，只依赖 OpenCV 的 core 和 imgproc。测试帧在内存中生成，笔记本上即可运行。

| 测试组 | 覆盖范围 |
| --- | --- |
| 输出尺寸与底图 | 将带有三块不同 BGR 颜色的 1440×1080 帧缩放到 640×480 和 720×360。检查尺寸、类型、色块分界两侧和屏幕边缘的像素。 |
| 非等比图形缩放 | 960×640 → 480×480，横纵比例分别为 1/2 和 3/4。检查四边形的四个顶点、四边中点与内部，跟踪圆周，以及箭头起点、箭杆、端点和箭翼。 |
| 可选图形 | 分别传入全空、仅箭头端点、仅四边形、仅跟踪点，以及跟踪点加端点。检查各组合应出现的图形和应留空的区域。 |
| 固定文字行 | 在 640×480 的黑底上，检查四角共 13 个行位。以可选文字齐全的画面为参照，再测全缺和每次仅有一个可选字段。按行检查白字是否存在、保留行是否移位，以及缺少实际速度时右下整块是否为空。 |
| 输入帧完整性 | 对带行间距的 Mat 子图做同尺寸绘制和缩放，与其连续副本的输出对照。绘制后和改写返回图后，均逐像素核对原始父图。 |

## 运行

在项目根目录构建主机测试目标。完整的 `display` 库依赖板端 MaixCDK。

```sh
cmake -S vision/display -B /tmp/display-test -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/display-test --target display_render_test
ctest --test-dir /tmp/display-test --output-on-failure
```

CTest 将五组检查注册为一个测试。加 `-V` 可查看每组的结果。文字检查只使用像素位置和颜色，无需 OCR。[审查与本次运行结果](../docs/display-review.md)
