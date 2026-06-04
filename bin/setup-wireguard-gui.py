#!/usr/bin/env python3
"""Create a local Python environment for the WireGuard GUI."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import venv
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
VENV_DIR = REPO_ROOT / ".wireguard-gui-venv"
PROTO_DIR = REPO_ROOT / "protobufs" / "meshtastic"
WORK_PROTO_ROOT = VENV_DIR / "generated-proto-src"
GENERATED_ROOT = VENV_DIR / "generated-protobufs"


def _venv_python() -> Path:
    if sys.platform == "win32":
        return VENV_DIR / "Scripts" / "python.exe"
    return VENV_DIR / "bin" / "python"


def _run(command: list[str], *, cwd: Path | None = None) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=cwd or REPO_ROOT, check=True)


def _create_venv(clear: bool) -> Path:
    if clear and VENV_DIR.exists():
        shutil.rmtree(VENV_DIR)
    if not VENV_DIR.exists():
        venv.EnvBuilder(with_pip=True).create(VENV_DIR)
    return _venv_python()


def _install_dependencies(python: Path) -> None:
    _run([str(python), "-m", "pip", "install", "--upgrade", "pip"])
    _run([str(python), "-m", "pip", "install", "meshtastic", "grpcio-tools"])


def _copy_transformed_protos() -> list[Path]:
    if WORK_PROTO_ROOT.exists():
        shutil.rmtree(WORK_PROTO_ROOT)
    target_dir = WORK_PROTO_ROOT / "meshtastic" / "protobuf"
    target_dir.mkdir(parents=True)

    transformed: list[Path] = []
    for proto in PROTO_DIR.glob("*.proto"):
        text = proto.read_text(encoding="utf-8")
        text = text.replace("package meshtastic;", "package meshtastic.protobuf;")
        text = text.replace('"meshtastic/', '"meshtastic/protobuf/')
        target = target_dir / proto.name
        target.write_text(text, encoding="utf-8")
        transformed.append(target.relative_to(WORK_PROTO_ROOT))
    return transformed


def _generate_branch_protobufs(python: Path, protos: list[Path]) -> None:
    if GENERATED_ROOT.exists():
        shutil.rmtree(GENERATED_ROOT)
    GENERATED_ROOT.mkdir(parents=True)

    command = [
        str(python),
        "-m",
        "grpc_tools.protoc",
        "-I",
        str(WORK_PROTO_ROOT),
        "-I",
        str(REPO_ROOT / "protobufs"),
        "--python_out",
        str(GENERATED_ROOT),
        *[str(proto).replace("\\", "/") for proto in protos],
    ]
    _run(command, cwd=WORK_PROTO_ROOT)


def _install_branch_protobufs(python: Path) -> None:
    code = "import meshtastic.protobuf, pathlib; print(pathlib.Path(meshtastic.protobuf.__file__).parent)"
    package_dir = subprocess.check_output([str(python), "-c", code], text=True).strip()
    target = Path(package_dir)
    source = GENERATED_ROOT / "meshtastic" / "protobuf"

    for generated in source.glob("*_pb2.py"):
        shutil.copy2(generated, target / generated.name)

    check = (
        "from meshtastic.protobuf import module_config_pb2; "
        "m = module_config_pb2.ModuleConfig(); "
        "raise SystemExit(0 if hasattr(m, 'wireguard') else 1)"
    )
    _run([str(python), "-c", check])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recreate", action="store_true", help="Delete and recreate the local GUI environment.")
    args = parser.parse_args()

    python = _create_venv(args.recreate)
    _install_dependencies(python)
    protos = _copy_transformed_protos()
    _generate_branch_protobufs(python, protos)
    _install_branch_protobufs(python)

    print()
    print(f"WireGuard GUI environment is ready: {VENV_DIR}")
    print("Launch with: bin\\wireguard-gui.cmd")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
