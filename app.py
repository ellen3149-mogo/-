import json
import subprocess
import time
import uuid
from pathlib import Path

from flask import Flask, jsonify, render_template, request, session

BASE_DIR = Path(__file__).resolve().parent
BACKEND_DIR = BASE_DIR / "backend"
RUNTIME_DIR = BASE_DIR / "runtime"
ENGINE_EXE = BACKEND_DIR / "minesweeper_engine.exe"
ENGINE_SRC = BACKEND_DIR / "minesweeper_engine.c"
BEST_FILE = RUNTIME_DIR / "best_records.json"

app = Flask(__name__)
app.secret_key = "replace-this-with-a-random-secret-key"


def ensure_backend_compiled() -> None:
    if ENGINE_EXE.exists():
        try:
            if ENGINE_EXE.stat().st_mtime >= ENGINE_SRC.stat().st_mtime:
                return
        except OSError:
            pass
    cmd = ["gcc", str(ENGINE_SRC), "-O2", "-o", str(ENGINE_EXE)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Backend compile failed: {result.stderr}")


def run_engine(command: str, *args: str) -> dict:
    ensure_backend_compiled()
    cmd = [str(ENGINE_EXE), command, *map(str, args)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 and not result.stdout:
        details = result.stderr.strip() or f"exit code {result.returncode}"
        raise RuntimeError(f"C backend execution failed: {details}")
    try:
        payload = json.loads(result.stdout.strip())
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid backend output: {result.stdout}") from exc
    if not payload.get("ok"):
        raise RuntimeError(payload.get("error", "Unknown backend error"))
    return payload


def get_state_file() -> Path | None:
    games = session.get("games")
    if not isinstance(games, dict):
        return None
    active_game_id = session.get("active_game_id")
    if not active_game_id:
        return None
    state_name = games.get(active_game_id)
    if not isinstance(state_name, str):
        return None
    path = (RUNTIME_DIR / state_name).resolve()
    try:
        path.relative_to(RUNTIME_DIR.resolve())
    except ValueError:
        return None
    return path


def ensure_session_games() -> dict:
    games = session.get("games")
    if not isinstance(games, dict):
        games = {}
        session["games"] = games
    return games


def get_active_game_id() -> str | None:
    value = session.get("active_game_id")
    return value if isinstance(value, str) else None


def read_json_file(path: Path, default: object) -> object:
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return default


def write_json_file(path: Path, payload: object) -> None:
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def best_records_load() -> dict:
    data = read_json_file(BEST_FILE, {})
    return data if isinstance(data, dict) else {}


def best_records_save(data: dict) -> None:
    RUNTIME_DIR.mkdir(exist_ok=True)
    write_json_file(BEST_FILE, data)


def board_key(game: dict) -> str:
    return f'{game.get("rows", 0)}x{game.get("cols", 0)}x{game.get("mines", 0)}'


def enrich_game_payload(game_id: str, game: dict) -> dict:
    starts = session.get("game_starts")
    if not isinstance(starts, dict):
        starts = {}
        session["game_starts"] = starts
    start_at = starts.get(game_id, int(time.time()))
    elapsed = max(0, int(time.time()) - int(start_at))
    game["elapsed_seconds"] = elapsed
    game["active_game_id"] = game_id
    records = best_records_load()
    game["best_record_seconds"] = records.get(board_key(game))
    return game


@app.route("/", methods=["GET"])
def index():
    return render_template("index.html")


def parse_int(value: object, default: int) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def get_request_json() -> dict:
    payload = request.get_json(silent=True)
    if isinstance(payload, dict):
        return payload
    return {}


@app.route("/api/new", methods=["POST"])
def new_game():
    payload = get_request_json()
    rows = parse_int(payload.get("rows"), 9)
    cols = parse_int(payload.get("cols"), 9)
    mines = parse_int(payload.get("mines"), 10)
    RUNTIME_DIR.mkdir(exist_ok=True)
    game_id = uuid.uuid4().hex
    state_file = RUNTIME_DIR / f"{uuid.uuid4().hex}.ms"

    try:
        game = run_engine("init", state_file, rows, cols, mines)
        games = ensure_session_games()
        games[game_id] = state_file.name
        session["games"] = games
        session["active_game_id"] = game_id

        starts = session.get("game_starts")
        if not isinstance(starts, dict):
            starts = {}
        starts[game_id] = int(time.time())
        session["game_starts"] = starts

        return jsonify(enrich_game_payload(game_id, game))
    except RuntimeError as exc:
        return jsonify({"ok": False, "error": str(exc)}), 400


def apply_action(action_name: str) -> tuple[dict, int]:
    payload = get_request_json()
    row = parse_int(payload.get("r"), -1)
    col = parse_int(payload.get("c"), -1)
    state_file = get_state_file()
    game_id = get_active_game_id()
    if not state_file or not state_file.exists():
        return {"ok": False, "error": "No active game. Please start a new game."}, 400
    if not game_id:
        return {"ok": False, "error": "No active game session."}, 400

    if row < 0 or col < 0:
        return {"ok": False, "error": "Invalid position."}, 400

    try:
        game = run_engine(action_name, state_file, row, col)
        if game.get("game_over") and game.get("won"):
            starts = session.get("game_starts", {})
            start_at = starts.get(game_id, int(time.time()))
            duration = max(0, int(time.time()) - int(start_at))
            key = board_key(game)
            records = best_records_load()
            current = records.get(key)
            if current is None or duration < int(current):
                records[key] = duration
                best_records_save(records)
        return enrich_game_payload(game_id, game), 200
    except RuntimeError as exc:
        return {"ok": False, "error": str(exc)}, 400


@app.route("/api/reveal", methods=["POST"])
def reveal():
    payload, status = apply_action("reveal")
    return jsonify(payload), status


@app.route("/api/flag", methods=["POST"])
def flag():
    payload, status = apply_action("flag")
    return jsonify(payload), status


@app.route("/api/state", methods=["GET"])
def state():
    state_file = get_state_file()
    game_id = get_active_game_id()
    if not state_file or not state_file.exists():
        return jsonify({"ok": False, "error": "No active game."}), 404
    if not game_id:
        return jsonify({"ok": False, "error": "No active game."}), 404
    try:
        game = run_engine("status", state_file)
        return jsonify(enrich_game_payload(game_id, game))
    except RuntimeError as exc:
        return jsonify({"ok": False, "error": str(exc)}), 400


@app.route("/api/undo", methods=["POST"])
def undo():
    state_file = get_state_file()
    game_id = get_active_game_id()
    if not state_file or not state_file.exists() or not game_id:
        return jsonify({"ok": False, "error": "No active game."}), 400

    try:
        game = run_engine("undo", state_file)
        return jsonify(enrich_game_payload(game_id, game))
    except RuntimeError as exc:
        return jsonify({"ok": False, "error": str(exc)}), 400


@app.route("/api/games", methods=["GET"])
def list_games():
    games = ensure_session_games()
    starts = session.get("game_starts", {})
    active = get_active_game_id()
    items = []
    for game_id, state_name in games.items():
        path = RUNTIME_DIR / state_name
        if not path.exists():
            continue
        try:
            game = run_engine("status", path)
        except RuntimeError:
            continue
        items.append(
            {
                "game_id": game_id,
                "rows": game.get("rows"),
                "cols": game.get("cols"),
                "mines": game.get("mines"),
                "game_over": game.get("game_over"),
                "won": game.get("won"),
                "elapsed_seconds": max(0, int(time.time()) - int(starts.get(game_id, int(time.time())))),
                "is_active": game_id == active,
            }
        )
    return jsonify({"ok": True, "games": items, "active_game_id": active})


@app.route("/api/switch_game", methods=["POST"])
def switch_game():
    payload = get_request_json()
    game_id = payload.get("game_id")
    games = ensure_session_games()
    if not isinstance(game_id, str) or game_id not in games:
        return jsonify({"ok": False, "error": "Invalid game id."}), 400
    state_file = RUNTIME_DIR / games[game_id]
    if not state_file.exists():
        return jsonify({"ok": False, "error": "Game data does not exist."}), 400
    session["active_game_id"] = game_id
    try:
        game = run_engine("status", state_file)
        return jsonify(enrich_game_payload(game_id, game))
    except RuntimeError as exc:
        return jsonify({"ok": False, "error": str(exc)}), 400


if __name__ == "__main__":
    app.run(debug=True)
