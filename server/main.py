"""flux-api: 仿真引擎 HTTP API 服务。"""

import io
import zipfile
from contextlib import asynccontextmanager
from urllib.parse import quote

from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field

from .manager import InstanceManager


@asynccontextmanager
async def lifespan(_: FastAPI):
    yield
    manager.shutdown()


app = FastAPI(title="flux-api", lifespan=lifespan)
manager = InstanceManager()


# request models

class CreateInstanceRequest(BaseModel):
    instance_name: str = Field(min_length=1, max_length=128)


class CreateRunRequest(BaseModel):
    model_content: str = Field(min_length=1)
    external_files: dict[str, str] | None = None
    random_seed: int = 42


# instances

@app.post("/api/instances", status_code=201)
def create_instance(req: CreateInstanceRequest):
    state = manager.create_instance(req.instance_name)
    return state.to_dict()


@app.get("/api/instances")
def list_instances():
    return {"instances": [s.to_dict() for s in manager.list_instances()]}


@app.get("/api/instances/{instance_id}")
def get_instance(instance_id: str):
    state = manager.get_instance(instance_id)
    if state is None:
        raise HTTPException(404, "instance not found")
    return state.to_dict(include_runs=True)


@app.patch("/api/instances/{instance_id}")
def rename_instance(instance_id: str, req: CreateInstanceRequest):
    state = manager.rename_instance(instance_id, req.instance_name)
    if state is None:
        raise HTTPException(404, "instance not found")
    return state.to_dict()


@app.delete("/api/instances/{instance_id}")
def delete_instance(instance_id: str):
    state = manager.delete_instance(instance_id)
    if state is None:
        raise HTTPException(404, "instance not found")
    return {"instance_id": instance_id, "status": "deleted"}


# runs

def _get_run_or_404(instance_id: str, run_id: str):
    state = manager.get_run(run_id)
    if state is None or state.instance_id != instance_id:
        raise HTTPException(404, "run not found")
    return state


def _get_instance_or_404(instance_id: str):
    instance = manager.get_instance(instance_id)
    if instance is None:
        raise HTTPException(404, "instance not found")
    return instance


@app.post("/api/instances/{instance_id}/runs", status_code=201)
def create_run(instance_id: str, req: CreateRunRequest):
    state = manager.create_run(instance_id, req.model_content,
                               req.external_files, req.random_seed)
    if state is None:
        raise HTTPException(404, "instance not found")
    instance = _get_instance_or_404(instance_id)
    return state.to_dict(instance.instance_name)


@app.get("/api/instances/{instance_id}/runs")
def list_runs(instance_id: str):
    instance = _get_instance_or_404(instance_id)
    return {"runs": [r.to_dict(instance.instance_name) for r in manager.list_runs(instance_id)]}


@app.get("/api/instances/{instance_id}/runs/{run_id}")
def get_run(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    instance = _get_instance_or_404(instance_id)
    return state.to_dict(instance.instance_name)


@app.get("/api/instances/{instance_id}/runs/{run_id}/reports")
def get_reports(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    if state.status != "completed":
        raise HTTPException(409, f"run is {state.status}, reports not ready")
    output_dir = state.run_dir / "output"
    csv_files = sorted(output_dir.glob("*.csv"))
    if not csv_files:
        raise HTTPException(404, "reports not found")

    instance = _get_instance_or_404(state.instance_id)

    ts = csv_files[0].stem.rsplit("-", 1)[-1]
    filename = f"{instance.instance_name}-reports-{ts}.zip"
    encoded = quote(filename)

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in csv_files:
            zf.write(path, path.name)
    buf.seek(0)

    return StreamingResponse(
        buf,
        media_type="application/zip",
        headers={"Content-Disposition": f"attachment; filename*=UTF-8''{encoded}"},
    )


@app.post("/api/instances/{instance_id}/runs/{run_id}/cancel")
def cancel_run(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    if state.status != "running":
        raise HTTPException(409, f"run is {state.status}, cannot cancel")
    state = manager.stop_run(state)
    return {"run_id": state.run_id, "status": state.status}


@app.delete("/api/instances/{instance_id}/runs/{run_id}")
def delete_run(instance_id: str, run_id: str):
    state = _get_run_or_404(instance_id, run_id)
    state = manager.delete_run(state)
    return {"run_id": state.run_id, "status": "deleted"}