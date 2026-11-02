"""Build the checked-in CubeIDE source sets with GNU Arm Embedded GCC."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=["all", "F103_ECU_Server", "F407_Tester"], default="all")
    parser.add_argument("--toolchain-bin", type=Path, help="Directory containing arm-none-eabi-gcc")
    args = parser.parse_args()

    def tool(name: str) -> str:
        if args.toolchain_bin:
            suffix = ".exe" if (args.toolchain_bin / (name + ".exe")).exists() else ""
            result = args.toolchain_bin / (name + suffix)
            if result.is_file():
                return str(result.resolve())
            parser.error(f"Missing tool: {result}")
        result = shutil.which(name)
        if not result:
            parser.error(f"Add {name} to PATH or supply --toolchain-bin")
        return result

    gcc, objcopy, size = (tool("arm-none-eabi-" + name) for name in ("gcc", "objcopy", "size"))
    config = json.loads((ROOT / "firmware/build_sources.json").read_text(encoding="utf-8"))
    for name, spec in config.items():
        if args.target not in ("all", name):
            continue
        base = ROOT / "firmware/targets" / name
        output = ROOT / "build" / name
        output.mkdir(parents=True, exist_ok=True)
        is_f1 = spec["family"] == "F1"
        device = "STM32F1xx" if is_f1 else "STM32F4xx"
        port = "ARM_CM3" if is_f1 else "ARM_CM4F"
        architecture = ["-mthumb", "-mcpu=" + ("cortex-m3" if is_f1 else "cortex-m4")]
        architecture += ["-mfloat-abi=soft"] if is_f1 else ["-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]
        includes = ["Inc", "Drivers/CMSIS/Include", f"Drivers/CMSIS/Device/ST/{device}/Include", f"Drivers/{device}_HAL_Driver/Inc", f"Drivers/{device}_HAL_Driver/Inc/Legacy", "Middlewares/Third_Party/FreeRTOS/Source/include", "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS", f"Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/{port}"]
        flags = architecture + ["-g3", "-O0", "-ffunction-sections", "-fdata-sections", "-Wall", "-DUSE_HAL_DRIVER", "-DDEBUG", "-D" + ("STM32F103xB" if is_f1 else "STM32F407xx"), "--specs=nano.specs"]
        flags += ["-I" + str(base / folder) for folder in includes]
        objects = []
        for source in spec["sources"]:
            obj = output / (source.replace("/", "_") + ".o")
            language = ["-std=gnu11"] if source.endswith(".c") else ["-x", "assembler-with-cpp"]
            subprocess.run([gcc, *flags, *language, "-c", str(base / source), "-o", str(obj)], check=True)
            objects.append(obj)
        linker = base / ("STM32F103C8TX_FLASH.ld" if is_f1 else "STM32F407VETX_FLASH.ld")
        elf = output / (name + ".elf")
        response = output / "objects.rsp"
        response.write_text("\n".join('"' + str(p).replace("\\", "/") + '"' for p in objects), encoding="utf-8")
        subprocess.run([gcc, "@" + str(response), *architecture, "-T" + str(linker), "--specs=nosys.specs", "--specs=nano.specs", "-static", "-Wl,--gc-sections", "-Wl,-Map=" + str(output / (name + ".map")), "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group", "-o", str(elf)], check=True)
        for format_, extension in (("ihex", ".hex"), ("binary", ".bin")):
            subprocess.run([objcopy, "-O", format_, str(elf), str(elf.with_suffix(extension))], check=True)
        subprocess.run([size, str(elf)], check=True)
        print(f"Build PASS: {name}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
