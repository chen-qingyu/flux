"""SQLite 持久化层。"""

import sqlite3
from pathlib import Path

DB_PATH = Path("instances") / "instances.db"


def init_db() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS instances (
            instance_id  TEXT PRIMARY KEY,
            instance_name TEXT NOT NULL,
            created_at   TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS runs (
            run_id       TEXT PRIMARY KEY,
            instance_id  TEXT NOT NULL REFERENCES instances(instance_id) ON DELETE CASCADE,
            run_name     TEXT NOT NULL DEFAULT '',
            status       TEXT NOT NULL DEFAULT 'running',
            error        TEXT,
            random_seed  INTEGER NOT NULL DEFAULT 42,
            created_at   TEXT NOT NULL
        );
    """)
    return conn


# instances

def insert_instance(conn: sqlite3.Connection, instance_id: str, instance_name: str, created_at: str):
    conn.execute(
        "INSERT INTO instances (instance_id, instance_name, created_at) VALUES (?, ?, ?)",
        (instance_id, instance_name, created_at))
    conn.commit()


def update_instance(conn: sqlite3.Connection, instance_id: str, instance_name: str):
    conn.execute("UPDATE instances SET instance_name = ? WHERE instance_id = ?", (instance_name, instance_id))
    conn.commit()


def delete_instance(conn: sqlite3.Connection, instance_id: str):
    conn.execute("DELETE FROM instances WHERE instance_id = ?", (instance_id,))
    conn.commit()


def list_instances(conn: sqlite3.Connection) -> list[dict]:
    rows = conn.execute(
        "SELECT instance_id, instance_name, created_at FROM instances ORDER BY created_at DESC"
    ).fetchall()
    return [{"instance_id": r[0], "instance_name": r[1], "created_at": r[2]} for r in rows]


# runs

def insert_run(conn: sqlite3.Connection, run_id: str, instance_id: str,
               run_name: str, status: str, random_seed: int, created_at: str):
    conn.execute(
        "INSERT INTO runs (run_id, instance_id, run_name, status, random_seed, created_at) VALUES (?, ?, ?, ?, ?, ?)",
        (run_id, instance_id, run_name, status, random_seed, created_at))
    conn.commit()


def update_run(conn: sqlite3.Connection, run_id: str,
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
    params.append(run_id)
    conn.execute(f"UPDATE runs SET {', '.join(fields)} WHERE run_id = ?", params)
    conn.commit()


def delete_run(conn: sqlite3.Connection, run_id: str):
    conn.execute("DELETE FROM runs WHERE run_id = ?", (run_id,))
    conn.commit()


def list_runs(conn: sqlite3.Connection) -> list[dict]:
    rows = conn.execute(
        "SELECT run_id, instance_id, run_name, status, error, random_seed, created_at FROM runs ORDER BY created_at DESC"
    ).fetchall()
    return [{"run_id": r[0], "instance_id": r[1], "run_name": r[2],
             "status": r[3], "error": r[4], "random_seed": r[5], "created_at": r[6]} for r in rows]
