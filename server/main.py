"""flux-api: 仿真引擎 HTTP API 服务。"""

import io
import zipfile

from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel

from .manager import InstanceManager

app = FastAPI(title="flux-api")
manager = InstanceManager()


class CreateInstanceRequest(BaseModel):
    model_name: str
    model_content: str
    external_files: dict[str, str] | None = None
    random_seed: int = 42


@app.post("/api/instances", status_code=201)
def create_instance(req: CreateInstanceRequest):
    state = manager.create(
        req.model_name, req.model_content,
        req.external_files, req.random_seed
    )
    return {
        "instance_id": state.instance_id,
        "model_name": state.model_name,
        "status": state.status,
        "created_at": state.created_at,
    }


@app.get("/api/instances")
def list_instances():
    return {
        "instances": [
            {
                "instance_id": s.instance_id,
                "model_name": s.model_name,
                "status": s.status,
                "created_at": s.created_at,
            }
            for s in manager.list_all()
        ]
    }


@app.get("/api/instances/{instance_id}")
def get_instance(instance_id: str):
    state = manager.get(instance_id)
    if state is None:
        raise HTTPException(404, "instance not found")
    return {
        "instance_id": state.instance_id,
        "model_name": state.model_name,
        "status": state.status,
        "reports": state.reports() if state.status == "completed" else [],
        "error": state.error,
        "created_at": state.created_at,
    }


@app.get("/api/instances/{instance_id}/reports")
def get_reports(instance_id: str):
    state = manager.get(instance_id)
    if state is None:
        raise HTTPException(404, "instance not found")
    output_dir = state.instance_dir / "output"
    if not output_dir.exists():
        raise HTTPException(404, "reports not found")

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(output_dir.glob("*.csv")):
            zf.write(path, path.name)
    buf.seek(0)

    return StreamingResponse(
        buf,
        media_type="application/zip",
        headers={"Content-Disposition": f"attachment; filename={state.model_name}-reports.zip"},
    )


@app.delete("/api/instances/{instance_id}")
def cancel_instance(instance_id: str):
    if not manager.cancel(instance_id):
        raise HTTPException(404, "instance not found")
    return {"instance_id": instance_id, "status": "cancelled"}
