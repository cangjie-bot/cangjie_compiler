from __future__ import annotations

from pathlib import Path

import svgwrite


def create_chir_flow_svg(target: Path) -> None:
    width, height = 1200, 500
    dwg = svgwrite.Drawing(str(target), size=(f"{width}px", f"{height}px"))
    dwg.add(dwg.rect(insert=(0, 0), size=(width, height), fill="#08162D"))

    title = "CHIR 阶段插件执行流程"
    dwg.add(
        dwg.text(
            title,
            insert=(width / 2, 60),
            text_anchor="middle",
            font_size=32,
            font_family="Microsoft YaHei, Arial",
            fill="#F3F7FF",
        )
    )

    boxes = [
        ("CHIR Builder 初始化", "构建 CHIRPluginManager\nci.metaTransformPluginBuilder"),
        ("遍历插件概念", "ForEachMetaTransformConcept\n依次获取 MetaTransform"),
        ("类别判定", "IsForFunc / IsForPackage\n决定针对函数或包"),
        ("执行 Run", "Func：遍历 package funcs\nPackage：直接运行"),
        ("质量保障", "Profile 记录\n异常诊断 + IRCheck"),
    ]

    box_width = 200
    box_height = 120
    gap = 40
    start_x = (width - (len(boxes) * box_width + (len(boxes) - 1) * gap)) / 2
    start_y = 140

    colors = ["#00A4FF", "#9966FF", "#00CEAD", "#FFA95C", "#36C6FF"]

    for idx, (title_text, desc_text) in enumerate(boxes):
        x = start_x + idx * (box_width + gap)
        y = start_y
        dwg.add(
            dwg.rect(
                insert=(x, y),
                size=(box_width, box_height),
                rx=16,
                ry=16,
                fill=colors[idx % len(colors)],
                stroke="#F3F7FF",
                stroke_width=2,
            )
        )
        dwg.add(
            dwg.text(
                title_text,
                insert=(x + box_width / 2, y + 35),
                text_anchor="middle",
                font_size=18,
                font_family="Microsoft YaHei, Arial",
                font_weight="bold",
                fill="#181C28",
            )
        )
        desc_lines = desc_text.split("\n")
        for line_idx, line in enumerate(desc_lines):
            dwg.add(
                dwg.text(
                    line,
                    insert=(x + box_width / 2, y + 65 + line_idx * 20),
                    text_anchor="middle",
                    font_size=14,
                    font_family="Microsoft YaHei, Arial",
                    fill="#181C28",
                )
            )

        if idx < len(boxes) - 1:
            arrow_x = x + box_width
            arrow_y = y + box_height / 2
            dwg.add(
                dwg.line(
                    start=(arrow_x + 5, arrow_y),
                    end=(arrow_x + gap - 5, arrow_y),
                    stroke="#00CEAD",
                    stroke_width=4,
                    stroke_dasharray="4,4",
                )
            )
            dwg.add(
                dwg.polygon(
                    points=[
                        (arrow_x + gap - 5, arrow_y),
                        (arrow_x + gap - 15, arrow_y - 8),
                        (arrow_x + gap - 15, arrow_y + 8),
                    ],
                    fill="#00CEAD",
                )
            )

    note = (
        "异常：捕获后触发 plugin_throws_exception 诊断；"
        "成功并启用 IRCheck 时继续进行 IRCheck(package, opts, builder, Phase::PLUGIN)。"
    )
    dwg.add(
        dwg.text(
            note,
            insert=(width / 2, height - 60),
            text_anchor="middle",
            font_size=16,
            font_family="Microsoft YaHei, Arial",
            fill="#F3F7FF",
        )
    )

    dwg.save()


if __name__ == "__main__":
    output_svg = Path(__file__).with_name("CHIR_Plugin_Flow.svg")
    create_chir_flow_svg(output_svg)
    print(f"SVG exported to {output_svg}")

