import shutil
import subprocess
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext as _build_ext
from setuptools.command.sdist import sdist as _sdist


def _project_root() -> Path:
    here = Path(__file__).resolve().parent
    for root in (here, here.parent):
        if (root / "xmake.lua").exists():
            return root
    raise RuntimeError("Unable to locate xmake.lua.")


class build_ext(_build_ext):
    def run(self):
        root = _project_root()
        subprocess.run(["xmake", "build", "_native"], cwd=root, check=True)
        artifacts = sorted(root.glob("build/**/_native*.pyd")) + sorted(root.glob("build/**/_native*.so"))
        if not artifacts:
            raise RuntimeError("Unable to locate native flux module.")
        destination = Path(self.get_ext_fullpath("flux._native"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(artifacts[-1], destination)


class sdist(_sdist):
    def make_release_tree(self, base_dir: str, files: list[str]):
        super().make_release_tree(base_dir, files)
        root = _project_root()
        for name in ("readme.md", "xmake.lua", "src", "tests"):
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
