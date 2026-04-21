const boardEl = document.getElementById("board");
const statusEl = document.getElementById("status");
const resultEl = document.getElementById("result");
const errorEl = document.getElementById("error");
const formEl = document.getElementById("new-game-form");
const rowsInput = document.getElementById("rows");
const colsInput = document.getElementById("cols");
const minesInput = document.getElementById("mines");
const undoBtn = document.getElementById("undo-btn");
const timerEl = document.getElementById("timer");
const bestRecordEl = document.getElementById("best-record");
const gameListEl = document.getElementById("game-list");
const refreshGamesBtn = document.getElementById("refresh-games-btn");
const activeGameHintEl = document.getElementById("active-game-hint");
const historyListEl = document.getElementById("history-list");

let currentGame = null;

function showError(message) {
    if (!message) {
        errorEl.textContent = "";
        errorEl.classList.add("hidden");
        return;
    }
    errorEl.textContent = message;
    errorEl.classList.remove("hidden");
}

async function api(path, payload) {
    const options = {
        method: payload ? "POST" : "GET",
        headers: {"Content-Type": "application/json"}
    };
    if (payload) {
        options.body = JSON.stringify(payload);
    }
    const res = await fetch(path, options);
    const data = await res.json();
    if (!res.ok || !data.ok) {
        throw new Error(data.error || "Request failed.");
    }
    return data;
}

function cellLabel(cell) {
    if (cell === ".") return "";
    return cell;
}

function applyResultText(game) {
    if (!game.game_over) {
        resultEl.textContent = "";
        resultEl.className = "result hidden";
        return;
    }
    if (game.won) {
        resultEl.textContent = "恭喜，你贏了。";
        resultEl.className = "result win";
    } else {
        resultEl.textContent = "踩到地雷，遊戲結束。";
        resultEl.className = "result lose";
    }
}

function renderBoard(game) {
    boardEl.innerHTML = "";
    boardEl.style.gridTemplateColumns = `repeat(${game.cols}, 36px)`;
    for (let r = 0; r < game.rows; r += 1) {
        for (let c = 0; c < game.cols; c += 1) {
            const cell = game.board[r][c];
            const btn = document.createElement("button");
            btn.type = "button";
            btn.className = "cell-btn";
            btn.dataset.row = String(r);
            btn.dataset.col = String(c);

            const hidden = cell === "#" || cell === "F";
            const flagged = cell === "F";
            if (!hidden) btn.classList.add("revealed");
            if (flagged) btn.classList.add("flagged");
            if (cell === "*") btn.classList.add("mine");

            btn.textContent = cellLabel(cell);
            btn.disabled = game.game_over || (!hidden && cell !== "F");

            btn.addEventListener("click", async () => {
                if (game.game_over) return;
                if (flagged) return;
                await doAction("/api/reveal", r, c);
            });
            btn.addEventListener("contextmenu", async (event) => {
                event.preventDefault();
                if (game.game_over) return;
                await doAction("/api/flag", r, c);
            });
            boardEl.appendChild(btn);
        }
    }
}

function render(game) {
    currentGame = game;
    statusEl.textContent = `尺寸 ${game.rows} x ${game.cols} | 地雷 ${game.mines} | 已翻開 ${game.revealed}`;
    statusEl.classList.remove("hidden");
    timerEl.textContent = `計時：${game.elapsed_seconds ?? 0} 秒`;
    timerEl.classList.remove("hidden");
    if (typeof game.best_record_seconds === "number") {
        bestRecordEl.textContent = `此難度最佳：${game.best_record_seconds} 秒`;
    } else {
        bestRecordEl.textContent = "此難度最佳：尚無紀錄";
    }
    bestRecordEl.classList.remove("hidden");
    activeGameHintEl.textContent = game.active_game_id ? `目前局號：${game.active_game_id.slice(0, 8)}` : "";
    applyResultText(game);
    renderBoard(game);
    renderHistory(game.history || []);
    refreshGames().catch(() => {});
}

function renderHistory(history) {
    historyListEl.innerHTML = "";
    if (!history.length) {
        const li = document.createElement("li");
        li.textContent = "尚無操作。";
        historyListEl.appendChild(li);
        return;
    }
    history.slice(-20).forEach((node) => {
        const li = document.createElement("li");
        li.textContent = `${node.action} (${node.r}, ${node.c}) | revealed=${node.revealed} | t=${node.timestamp}`;
        historyListEl.appendChild(li);
    });
}

function renderGames(games, activeGameId) {
    gameListEl.innerHTML = "";
    if (!games.length) {
        const li = document.createElement("li");
        li.textContent = "目前沒有可切換的遊戲。";
        gameListEl.appendChild(li);
        return;
    }
    games.forEach((g) => {
        const li = document.createElement("li");
        const btn = document.createElement("button");
        btn.type = "button";
        btn.textContent = `${g.rows}x${g.cols}/${g.mines} | ${g.elapsed_seconds}s ${g.game_over ? (g.won ? "已勝利" : "已失敗") : "進行中"} ${g.is_active ? " (目前)" : ""}`;
        btn.disabled = g.game_id === activeGameId;
        btn.addEventListener("click", async () => {
            try {
                showError("");
                const game = await api("/api/switch_game", {game_id: g.game_id});
                render(game);
            } catch (err) {
                showError(err.message);
            }
        });
        li.appendChild(btn);
        gameListEl.appendChild(li);
    });
}

async function refreshGames() {
    const data = await api("/api/games");
    renderGames(data.games || [], data.active_game_id);
}

async function doAction(path, r, c) {
    try {
        showError("");
        const game = await api(path, {r, c});
        render(game);
    } catch (err) {
        showError(err.message);
    }
}

formEl.addEventListener("submit", async (event) => {
    event.preventDefault();
    const rows = Number(rowsInput.value);
    const cols = Number(colsInput.value);
    const mines = Number(minesInput.value);
    try {
        showError("");
        const game = await api("/api/new", {rows, cols, mines});
        render(game);
    } catch (err) {
        showError(err.message);
    }
});

undoBtn.addEventListener("click", async () => {
    try {
        showError("");
        const game = await api("/api/undo", {});
        render(game);
    } catch (err) {
        showError(err.message);
    }
});

refreshGamesBtn.addEventListener("click", async () => {
    try {
        showError("");
        await refreshGames();
    } catch (err) {
        showError(err.message);
    }
});

async function boot() {
    try {
        const game = await api("/api/state");
        render(game);
    } catch {
        // no active game
    }
    try {
        await refreshGames();
    } catch {
        // ignore
    }
}

boot();
