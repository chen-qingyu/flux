"""实例与运行生命周期管理。"""

import multiprocessing as mp
import re
import shutil
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from . import db

INSTANCES_ROOT = Path("instances")


@dataclass
class RunState:
    run_id: str
    instance_id: str
    run_name: str
    status: str          # running | completed | failed | cancelled
    random_seed: int
    process: mp.Process | None
    run_dir: Path
    created_at: str
    error: str | None = None
    pipe: Any = None

    def reports(self) -> list[str]:
        out = self.run_dir / "output"
        if not out.exists():
            return []
        return sorted(p.name for p in out.glob("*.csv"))

    def to_dict(self, instance_name: str = "") -> dict:
        return {
            "run_id": self.run_id,
            "instance_id": self.instance_id,
            "instance_name": instance_name,
            "run_name": self.run_name,
            "status": self.status,
            "random_seed": self.random_seed,
            "reports": self.reports() if self.status == "completed" else [],
            "error": self.error,
            "created_at": self.created_at,
        }


@dataclass
class InstanceState:
    instance_id: str
    instance_name: str
    created_at: str
    runs: dict[str, RunState] = field(default_factory=dict)

    def to_dict(self, include_runs: bool = False) -> dict:
        d = {
            "instance_id": self.instance_id,
            "instance_name": self.instance_name,
            "created_at": self.created_at,
            "run_count": len(self.runs),
        }
        if include_runs:
            d["runs"] = [r.to_dict(self.instance_name) for r in sorted(
                self.runs.values(), key=lambda r: r.created_at, reverse=True)]
        return d


class InstanceManager:
    def __init__(self):
        self._instances: dict[str, InstanceState] = {}
        self._runs: dict[str, RunState] = {}
        self._conn = db.init_db()
        self._load_all()

    # instances

    def create_instance(self, instance_name: str) -> InstanceState:
        instance_name = re.sub(r'[\\/:*?"<>|]', '_', instance_name)
        instance_id = uuid.uuid4().hex[:12]
        created_at = datetime.now(timezone.utc).isoformat()
        (INSTANCES_ROOT / instance_id).mkdir(parents=True)
        db.insert_instance(self._conn, instance_id, instance_name, created_at)

        state = InstanceState(instance_id=instance_id, instance_name=instance_name, created_at=created_at)
        self._instances[instance_id] = state
        return state

    def get_instance(self, instance_id: str) -> InstanceState | None:
        return self._instances.get(instance_id)

    def list_instances(self) -> list[InstanceState]:
        return sorted(self._instances.values(), key=lambda s: s.created_at, reverse=True)

    def rename_instance(self, instance_id: str, instance_name: str) -> InstanceState | None:
        state = self._instances.get(instance_id)
        if state is None:
            return None
        instance_name = re.sub(r'[\\/:*?"<>|]', '_', instance_name)
        state.instance_name = instance_name
        db.update_instance(self._conn, instance_id, instance_name)
        return state

    def delete_instance(self, instance_id: str) -> InstanceState | None:
        state = self._instances.get(instance_id)
        if state is None:
            return None
        for run in list(state.runs.values()):
            self._kill_run(run)
        shutil.rmtree(INSTANCES_ROOT / instance_id, ignore_errors=True)
        db.delete_instance(self._conn, instance_id)
        for run in state.runs.values():
            self._runs.pop(run.run_id, None)
        del self._instances[instance_id]
        return state

    # runs

    def create_run(self, instance_id: str, model_content: str,
                   external_files: dict[str, str] | None = None,
                   random_seed: int = 42,
                   run_name: str | None = None) -> RunState | None:
        instance = self._instances.get(instance_id)
        if instance is None:
            return None

        run_id = uuid.uuid4().hex[:12]
        if run_name is None:
            run_name = instance.instance_name
        run_dir = INSTANCES_ROOT / instance_id / "runs" / run_id
        ext_dir = run_dir / "external"
        out_dir = run_dir / "output"
        ext_dir.mkdir(parents=True)
        out_dir.mkdir()
        created_at = datetime.now(timezone.utc).isoformat()

        (run_dir / "model.bpmn").write_text(model_content, encoding="utf-8")

        if external_files:
            for filename, csv_content in external_files.items():
                (ext_dir / f"{filename}.csv").write_text(csv_content, encoding="utf-8")

        db.insert_run(self._conn, run_id, instance_id, run_name, "running", random_seed, created_at)

        parent_conn, child_conn = mp.Pipe()
        p = mp.Process(
            target=_run_worker,
            args=(run_name, model_content,
                  str(ext_dir), str(out_dir), random_seed, child_conn)
        )
        p.start()

        state = RunState(
            run_id=run_id, instance_id=instance_id, run_name=run_name,
            status="running", random_seed=random_seed, process=p,
            pipe=parent_conn, run_dir=run_dir, created_at=created_at,
        )
        instance.runs[run_id] = state
        self._runs[run_id] = state
        return state

    def get_run(self, run_id: str) -> RunState | None:
        state = self._runs.get(run_id)
        if state is None:
            return None
        self._refresh_run(state)
        return state

    def list_runs(self, instance_id: str | None = None) -> list[RunState]:
        runs = self._runs.values()
        if instance_id:
            runs = [r for r in runs if r.instance_id == instance_id]
        for r in runs:
            self._refresh_run(r)
        return sorted(runs, key=lambda r: r.created_at, reverse=True)

    def rename_run(self, state: RunState, run_name: str) -> RunState:
        run_name = re.sub(r'[\\/:*?"<>|]', '_', run_name)
        state.run_name = run_name
        db.update_run(self._conn, state.run_id, run_name=run_name)
        return state

    def stop_run(self, state: RunState) -> RunState:
        self._kill_run(state)
        state.status = "cancelled"
        db.update_run(self._conn, state.run_id, status="cancelled")
        shutil.rmtree(state.run_dir, ignore_errors=True)
        return state

    def delete_run(self, state: RunState) -> RunState:
        self._kill_run(state)
        shutil.rmtree(state.run_dir, ignore_errors=True)
        db.delete_run(self._conn, state.run_id)
        instance = self._instances.get(state.instance_id)
        if instance:
            instance.runs.pop(state.run_id, None)
        self._runs.pop(state.run_id, None)
        return state

    # internal

    def _kill_run(self, state: RunState):
        if state.pipe:
            state.pipe.close()
            state.pipe = None
        if state.process and state.process.is_alive():
            state.process.terminate()
            state.process.join(timeout=5)

    def _refresh_run(self, state: RunState):
        if state.status != "running":
            return
        if state.pipe and state.pipe.poll():
            status, error = state.pipe.recv()
            state.status = status
            state.error = error
            state.pipe.close()
            state.pipe = None
            db.update_run(self._conn, state.run_id, status=status, error=error)
        elif state.process and not state.process.is_alive():
            state.status = "failed"
            state.error = f"exit code {state.process.exitcode}"
            db.update_run(self._conn, state.run_id, status="failed", error=state.error)

    def _load_all(self):
        for row in db.list_instances(self._conn):
            inst = InstanceState(
                instance_id=row["instance_id"],
                instance_name=row["instance_name"],
                created_at=row["created_at"],
            )
            self._instances[inst.instance_id] = inst

        for row in db.list_runs(self._conn):
            instance = self._instances.get(row["instance_id"])
            if instance is None:
                continue
            run_dir = INSTANCES_ROOT / row["instance_id"] / "runs" / row["run_id"]
            status = row["status"]
            error = row["error"]

            if status == "running":
                status = "failed"
                error = "server restarted"
                db.update_run(self._conn, row["run_id"], status=status, error=error)

            state = RunState(
                run_id=row["run_id"], instance_id=row["instance_id"],
                run_name=row["run_name"],
                status=status,
                random_seed=row["random_seed"], process=None,
                run_dir=run_dir, created_at=row["created_at"], error=error,
            )
            instance.runs[state.run_id] = state
            self._runs[state.run_id] = state

    def shutdown(self):
        for state in self._runs.values():
            self._refresh_run(state)
            if state.status == "running":
                self._kill_run(state)
                state.status = "failed"
                state.error = "server shutdown"
                db.update_run(self._conn, state.run_id, status=state.status, error=state.error)


def _run_worker(run_name: str, model_content: str,
                external_dir: str, output_dir: str, random_seed: int, conn):
    """模块级函数，确保 multiprocessing 可 pickle。"""
    try:
        import flux
        flux.run(run_name, model_content, output_dir, external_dir, random_seed)
        conn.send(("completed", ""))
    except Exception as e:
        conn.send(("failed", str(e)))
    finally:
        conn.close()
