#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel-generator", required=True, type=Path)
    parser.add_argument("--module-dir", required=True, type=Path)
    parser.add_argument("--target-cc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    module_dir = args.module_dir.resolve()
    target_cc = args.target_cc.absolute()
    output = args.output.resolve()

    if not target_cc.is_file():
        raise SystemExit(
            f"error: cross-compiler not found: {target_cc}; "
            "run ./scripts/build-rootfs.sh first"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(
        prefix=".compile_commands.", suffix=".json", dir=output.parent
    )
    os.close(fd)
    temporary = Path(temporary_name)

    try:
        subprocess.run(
            [
                sys.executable,
                str(args.kernel_generator.resolve()),
                "-d",
                str(module_dir),
                "-o",
                str(temporary),
                str(module_dir / "modules.order"),
            ],
            check=True,
        )
        entries = json.loads(temporary.read_text())
        source = module_dir / "user_test/edu_test.c"
        entries.append(
            {
                "arguments": [
                    str(target_cc),
                    "-Wall",
                    "-Wextra",
                    "-O2",
                    "-pthread",
                    f"-I{module_dir / 'include'}",
                    "-c",
                    str(source),
                    "-o",
                    str(module_dir / "user_test/edu_test.o"),
                ],
                "directory": str(module_dir),
                "file": str(source),
            }
        )
        temporary.write_text(json.dumps(entries, indent=2) + "\n")
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
