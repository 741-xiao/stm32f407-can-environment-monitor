from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def build(board_id: int, toolchain: Path) -> None:
    project = ROOT / "Project"
    output = project / "Build" / f"Board{board_id}"
    output.mkdir(parents=True, exist_ok=True)
    target = ET.parse(project / "LCD.uvprojx").find("./Targets/Target")
    if target is None:
        raise RuntimeError("Keil 工程没有编译目标")
    controls = target.find("./TargetOption/TargetArmAds/Cads/VariousControls")
    if controls is None:
        raise RuntimeError("Keil 工程没有编译配置")
    includes = [(project / path).resolve() for path in controls.findtext("IncludePath", "").split(";")]
    defines = controls.findtext("Define", "").split(",") + [f"APP_BOARD_ID={board_id}"]
    objects = []
    log = []

    def run(tool: str, *arguments: object) -> None:
        result = subprocess.run([str(toolchain / f"{tool}.exe"), *map(str, arguments)],
                                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        message = result.stdout.decode("utf-8", errors="replace")
        log.append(message)
        (output / "build.log").write_text("".join(log), encoding="utf-8")
        if message:
            print(message.rstrip())
        if result.returncode:
            raise SystemExit(result.returncode)

    for entry in target.findall("./Groups/Group/Files/File"):
        source = (project / entry.findtext("FilePath", "")).resolve()
        if source.suffix.lower() not in (".c", ".s"):
            continue
        obj = output / f"{source.stem}.o"
        objects.append(obj)
        if source.suffix.lower() == ".c":
            flags = ["--cpu=Cortex-M4.fp.sp", "--c99", "--no_multibyte_chars", "-O1", "-g", "--split_sections"]
            if source.parent.name in ("Main", "Serial", "WiFi", "SHT30", "OLED", "Light") or source.name == "led_effect.c":
                flags.append("--diag_error=warning")
            for include in includes:
                flags.extend(["-I", str(include)])
            for define in defines:
                flags.extend(["-D", define])
            run("armcc", *flags, "-c", source, "-o", obj)
        else:
            run("armasm", "--cpu=Cortex-M4.fp.sp", "--apcs=interwork", "-g", source, "-o", obj)
    scatter = output / "firmware.sct"
    scatter.write_text("LR_IROM1 0x08000000 0x00100000 {\n"
                       " ER_IROM1 0x08000000 0x00100000 { *.o (RESET, +First) *(InRoot$$Sections) .ANY (+RO) }\n"
                       " RW_IRAM1 0x20000000 0x00020000 { .ANY (+RW +ZI) }\n}\n", encoding="ascii")
    executable = output / f"Board{board_id}.axf"
    run("armlink", "--cpu=Cortex-M4.fp.sp", *objects, "--strict", "--scatter", scatter,
        "--summary_stderr", "--info=summarysizes", "--map", "--callgraph", "--symbols",
        "--list", output / "firmware.map", "-o", executable)
    run("fromelf", "--i32combined", "--output", output / f"Board{board_id}.hex", executable)
    print(f"Board {board_id}: {output / f'Board{board_id}.hex'}")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="使用 ARM Compiler 5 编译三块板的固件，不烧录")
    parser.add_argument("--board", type=int, choices=(1, 2, 3))
    parser.add_argument("--toolchain", type=Path, default=Path("C:/Keil/ARM/ARMCC/bin"))
    args = parser.parse_args()
    for board in (args.board,) if args.board else (1, 2, 3):
        build(board, args.toolchain)
