import json
import os
import subprocess
import uuid
from pathlib import Path

from flask import Flask, redirect, render_template, request, session, url_for

BASE_DIR = Path(__file__).resolve().parent
BACKEND_DIR = BASE_DIR / "backend"
RUNTIME_DIR = BASE_DIR / "runtime"
ENGINE_EXE = BACKEND_DIR / "minesweeper_engine.exe"
ENGINE_SRC = BACKEND_DIR / "minesweeper_engine.c"

app = Flask(__name__)
app.secret_key = "replace-this-with-a-random-secret-key"


def ensure_backend_compiled() -> None:
    if ENGINE_EXE.exists():
        return
    cmd = ["gcc", str(ENGINE_SRC), "-O2", "-o", str(ENGINE_EXE)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Backend compile failed: {result.stderr}")


def run_engine(command: str, *args: str) -> dict:
    ensure_backend_compiled()
    cmd = [str(ENGINE_EXE), command, *map(str, args)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 and not result.stdout:
        raise RuntimeError(result.stderr or "C backend execution failed.")
    try:
        payload = json.loads(result.stdout.strip())
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid backend output: {result.stdout}") from exc
    if not payload.get("ok"):
        raise RuntimeError(payload.get("error", "Unknown backend error"))
    return payload


def get_state_file() -> Path | None:
    state_name = session.get("state_file")
    if not state_name:
        return None
    path = (RUNTIME_DIR / state_name).resolve()
    try:
        path.relative_to(RUNTIME_DIR.resolve())
    except ValueError:
        return None
    return path


@app.route("/", methods=["GET"])
def index():
    game = None
    error = session.pop("error", None)
    state_file = get_state_file()
    if state_file and state_file.exists():
        try:
            game = run_engine("status", state_file)
        except RuntimeError as exc:
            error = str(exc)
    return render_template("index.html", game=game, error=error)


@app.route("/new-game", methods=["POST"])
def new_game():
    rows = int(request.form.get("rows", 9))
    cols = int(request.form.get("cols", 9))
    mines = int(request.form.get("mines", 10))
    RUNTIME_DIR.mkdir(exist_ok=True)
    state_file = RUNTIME_DIR / f"{uuid.uuid4().hex}.ms"

    try:
        run_engine("init", state_file, rows, cols, mines)
        session["state_file"] = state_file.name
    except RuntimeError as exc:
        session["error"] = str(exc)
    return redirect(url_for("index"))


@app.route("/action", methods=["POST"])
def action():
    row = int(request.form["row"])
    col = int(request.form["col"])
    action_name = request.form["action"]
    state_file = get_state_file()
    if not state_file or not state_file.exists():
        session["error"] = "No active game. Please start a new game."
        return redirect(url_for("index"))

    if action_name not in {"reveal", "flag"}:
        session["error"] = "Invalid action."
        return redirect(url_for("index"))

    try:
        run_engine(action_name, state_file, row, col)
    except RuntimeError as exc:
        session["error"] = str(exc)
    return redirect(url_for("index"))


if __name__ == "__main__":
    app.run(debug=True)
