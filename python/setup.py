from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext as _build_ext
from setuptools.command.sdist import sdist as _sdist


HERE = Path(__file__).resolve().parent


def _project_root() -> Path:
    for root in (HERE, HERE.parent):
        if (root / "xmake.lua").exists() and (root / "src").exists():
            return root
    raise RuntimeError("Unable to locate xmake.lua and src/ for the native build.")


class build_ext(_build_ext):
    def run(self) -> None:
        root = _project_root()
        subprocess.run(["xmake", "build", "_native"], cwd=root, check=True)
        artifacts = sorted(root.glob("build/**/_native*.pyd")) + sorted(root.glob("build/**/_native*.so"))
        if not artifacts:
            raise RuntimeError("xmake finished without producing a native flux module.")

        destination = Path(self.get_ext_fullpath("flux._native"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(artifacts[-1], destination)


class sdist(_sdist):
    def make_release_tree(self, base_dir: str, files: list[str]) -> None:
        super().make_release_tree(base_dir, files)
        root = _project_root()
        for name in ("README.md", "xmake.lua", "src", "tests"):
            source = root / name
            target = Path(base_dir) / name
            if source.is_dir():
                shutil.copytree(source, target, dirs_exist_ok=True)
            else:
                shutil.copy2(source, target)


setup(
    ext_modules=[Extension("flux._native", sources=[])],
    cmdclass={"build_ext": build_ext, "sdist": sdist},
)
