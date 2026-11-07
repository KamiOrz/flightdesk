# 可选飞机涂装

公开版本不捆绑原个人构建的 413 张飞机图或其衍生图。默认飞机图标和所有数据功能可以直接编译使用，产品照片中的涂装效果需要另外导入。

## 使用兼容的本地目录

1. 自行确认素材的许可和使用范围，再取得 PlaneSlate content 1.0.32 的兼容素材目录。上游：https://github.com/PlaneSlate/liveries/releases/tag/content-1.0.32 。这不是对上游素材可自由再分发的承诺。
2. 解压到项目 `.local/current-assets/`。保留目录结构，需包含 `index.jsonl`、`aircraft.jsonl`、`airlines.jsonl` 和它们引用的 PNG；脚本会递归定位这些索引。
3. 安装 `requirements-dev.txt` 后运行 `python3 tools/build_artwork.py`，再运行 `pio run`。导入器生成 `include/artwork.h`、`src/artwork.S` 和 `assets/*.rle`，并检查图像流尺寸与 Flash 总量。

自有图片可在 `assets/livery_overrides.json` 配置，例如：

```json
[{"airline":"CCA","type":"B738","path":"assets/custom/my-plane.png","tight_crop":true}]
```

图片须为完整侧视、透明背景、你有权使用的 PNG。脚本压缩到 248×64，并转为 RGB565；精确航司+机型优先，其次是 `*` 中性同机型，否则保留飞机图标。CN_AF 仅是内部补充图键。

不要把个人导入的素材、生成的 RLE 或其引用声明推送到公共仓库，除非你确认有再分发权。`.local/`、`assets/custom/`、`assets/*.rle` 已忽略；重新提交前仍应审查生成的两个源文件。
