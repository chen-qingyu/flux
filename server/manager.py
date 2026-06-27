"""仿真实例生命周期管理。"""

import multiprocessing as mp
import shutil
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

INSTANCES_ROOT = Path("instances")


@dataclass
class InstanceState:
    instance_id: str
    model_name: str
    status: str          # running | completed | failed | cancelled
    process: mp.Process | None
    instance_dir: Path
    created_at: str
    error: str | None = None

    def reports(self) -> list[str]:
        out = self.instance_dir / "output"
        if not out.exists():
            return []
        return sorted(p.name for p in out.glob("*.csv"))


class InstanceManager:
    def __init__(self):
        self._instances: dict[str, InstanceState] = {}
        INSTANCES_ROOT.mkdir(exist_ok=True)

    def create(self, model_name: str, model_content: str,
               external_files: dict[str, str] | None = None,
               random_seed: int = 42) -> InstanceState:
        instance_id = uuid.uuid4().hex[:12]
        instance_dir = INSTANCES_ROOT / instance_id
        ext_dir = instance_dir / "external"
        out_dir = instance_dir / "output"
        ext_dir.mkdir(parents=True)
        out_dir.mkdir()

        (instance_dir / "model.bpmn").write_text(model_content, encoding="utf-8")

        if external_files:
            for filename, csv_content in external_files.items():
                (ext_dir / f"{filename}.csv").write_text(csv_content, encoding="utf-8")

        p = mp.Process(
            target=_run_worker,
            args=(str(instance_dir), model_name, model_content,
                  str(ext_dir), str(out_dir), random_seed)
        )
        p.start()

        state = InstanceState(
            instance_id=instance_id,
            model_name=model_name,
            status="running",
            process=p,
            instance_dir=instance_dir,
            created_at=datetime.now(timezone.utc).isoformat(),
        )
        self._instances[instance_id] = state
        return state

    def get(self, instance_id: str) -> InstanceState | None:
        state = self._instances.get(instance_id)
        if state is None:
            return None
        self._refresh(state)
        return state

    def list_all(self) -> list[InstanceState]:
        for s in self._instances.values():
            self._refresh(s)
        return sorted(self._instances.values(),
                      key=lambda s: s.created_at, reverse=True)

    def stop(self, instance_id: str) -> InstanceState | None:
        """终止仿真：杀进程、删文件，保留状态记录。"""
        state = self._instances.get(instance_id)
        if state is None:
            return None
        if state.process and state.process.is_alive():
            state.process.terminate()
            state.process.join(timeout=5)
        state.status = "cancelled"
        shutil.rmtree(state.instance_dir, ignore_errors=True)
        return state

    def delete(self, instance_id: str) -> InstanceState | None:
        """删除实例：杀进程、删文件、移除内存记录。"""
        state = self._instances.get(instance_id)
        if state is None:
            return None
        if state.process and state.process.is_alive():
            state.process.terminate()
            state.process.join(timeout=5)
        shutil.rmtree(state.instance_dir, ignore_errors=True)
        del self._instances[instance_id]
        return state

    def _refresh(self, state: InstanceState):
        if state.status != "running":
            return
        status_file = state.instance_dir / "_status"
        if status_file.exists():
            text = status_file.read_text().strip()
            if text == "completed":
                state.status = "completed"
            elif text.startswith("failed:"):
                state.status = "failed"
                state.error = text[7:]
        elif state.process and not state.process.is_alive():
            state.status = "failed"
            state.error = f"exit code {state.process.exitcode}"


def _run_worker(instance_dir: str, model_name: str, model_content: str,
                external_dir: str, output_dir: str, random_seed: int):
    """模块级函数，确保 multiprocessing 可 pickle。"""
    status_file = Path(instance_dir) / "_status"
    try:
        import flux
        flux.run(model_name, model_content, output_dir, external_dir, random_seed)
        status_file.write_text("completed")
    except Exception as e:
        status_file.write_text(f"failed:{e}")
