"""SQLite 持久化层。"""

import sqlite3
from pathlib import Path

DB_PATH = Path("instances") / "instances.db"


def init_db() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS instances (
            instance_id  TEXT PRIMARY KEY,
            model_name   TEXT NOT NULL,
            status       TEXT NOT NULL DEFAULT 'running',
            error        TEXT,
            created_at   TEXT NOT NULL
        )
    """)
    conn.commit()
    return conn


def insert(conn: sqlite3.Connection, instance_id: str, model_name: str,
           status: str, created_at: str):
    conn.execute(
        "INSERT INTO instances (instance_id, model_name, status, created_at) VALUES (?, ?, ?, ?)",
        (instance_id, model_name, status, created_at))
    conn.commit()


def update(conn: sqlite3.Connection, instance_id: str,
           status: str | None = None, error: str | None = None):
    fields = []
    params = []
    if status is not None:
        fields.append("status = ?")
        params.append(status)
    if error is not None:
        fields.append("error = ?")
        params.append(error)
    if not fields:
        return
    params.append(instance_id)
    conn.execute(f"UPDATE instances SET {', '.join(fields)} WHERE instance_id = ?", params)
    conn.commit()


def delete(conn: sqlite3.Connection, instance_id: str):
    conn.execute("DELETE FROM instances WHERE instance_id = ?", (instance_id,))
    conn.commit()


def get(conn: sqlite3.Connection, instance_id: str) -> dict | None:
    row = conn.execute(
        "SELECT instance_id, model_name, status, error, created_at FROM instances WHERE instance_id = ?",
        (instance_id,)).fetchone()
    if row is None:
        return None
    return _row_to_dict(row)


def list_all(conn: sqlite3.Connection) -> list[dict]:
    rows = conn.execute(
        "SELECT instance_id, model_name, status, error, created_at FROM instances ORDER BY created_at DESC"
    ).fetchall()
    return [_row_to_dict(r) for r in rows]


def _row_to_dict(row: tuple) -> dict:
    return {
        "instance_id": row[0],
        "model_name": row[1],
        "status": row[2],
        "error": row[3],
        "created_at": row[4],
    }
