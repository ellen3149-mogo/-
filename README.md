# 踩地雷 Web 專題 (Flask x C)

本專題將經典踩地雷遊戲做成網頁版本，採用 **C 語言實作核心引擎**，由 **Python Flask** 提供 API，前端以 **HTML/CSS/JavaScript** 呈現互動介面。

## 1) 專題功能介紹

- 可自訂棋盤大小與地雷數（5~30 格）
- 左鍵 `reveal`、右鍵 `flag`
- 空白格自動展開（flood reveal）
- 勝敗判定（踩雷失敗 / 全部安全格翻開勝利）
- **Undo（上一步）**：每次操作都先存入 stack，可還原前一步
- **操作歷史（linked-list 形式）**：記錄整局完整操作序列
- **多人對戰 / session 多盤**：同一瀏覽器 session 可同時開多局並切換
- **計時器與最佳紀錄**：每局計時，並保存同難度最佳秒數

## 2) 使用技術

- 前端：`HTML5`、`CSS3`、`Vanilla JavaScript`
- 後端：`Python 3`、`Flask`
- C 核心引擎：`GCC` 編譯 `backend/minesweeper_engine.c`
- 跨語言整合：Flask 使用 `subprocess` 呼叫 C 執行檔並收 JSON
- 資料儲存：
  - 棋盤狀態：`runtime/*.ms`（binary）
  - Undo / 歷史（**由 C 引擎維護**）：`<state_file>.undo`、`<state_file>.hist`
  - 最佳紀錄：`runtime/best_records.json`

## 3) C 核心引擎：符合 PDF 規範的必備要求

以下項目可對應一般課堂 PDF 對 C 專題的要求：

- **結構化資料設計**：`Cell`、`GameHeader` 以 `struct` 管理狀態
- **動態記憶體管理**：`calloc/malloc/free` 建立盤面與 BFS queue
- **模組化函式**：`in_bounds`、`calculate_adjacency`、`flood_reveal`、`check_win`
- **檔案 I/O**：`save_game/load_game` 讀寫 binary state
- **演算法實作**：BFS 連鎖展開（空白區塊 reveal）
- **邊界與錯誤處理**：參數驗證、座標檢查、JSON 錯誤輸出
- **前後端介面明確**：以固定 JSON 欄位輸出給 Python/前端

## 4) 系統架構與執行方式（架構圖、目錄結構）

### 架構圖

```text
Browser (HTML/CSS/JS)
        │  HTTP/JSON
        ▼
Flask API (app.py)
        │  subprocess
        ▼
C Engine (backend/minesweeper_engine.exe)
        │
        ├─ runtime/*.ms                 (遊戲狀態)
        ├─ runtime/*.undo, *.hist       (C 引擎管理的 undo + history linked list)
        └─ runtime/best_records.json    (最佳紀錄)
```

### 目錄結構

```text
minesweeper_flask_c/
├── app.py
├── backend/
│   └── minesweeper_engine.c
├── templates/
│   └── index.html
├── static/
│   ├── game.js
│   └── style.css
├── runtime/
│   ├── *.ms
│   ├── *.ms.undo / *.ms.hist
│   └── best_records.json
└── README.md
```

## 5) 如何執行 (How to run)

1. 安裝 Python 套件

```bash
pip install -r requirements.txt
```

2. 啟動 Flask（Windows）

```bash
python app.py
```

3. 開啟瀏覽器

```text
http://127.0.0.1:5000
```

> 第一次啟動時，`app.py` 會自動以 `gcc` 編譯 `backend/minesweeper_engine.c` 產生 `minesweeper_engine.exe`。

## 6) 對外 API（C ↔ Python）

### Flask 提供給前端的主要 API

- `POST /api/new`：建立新局
- `POST /api/reveal`：翻格
- `POST /api/flag`：插旗
- `POST /api/undo`：回復上一步
- `GET /api/state`：讀取目前局面
- `GET /api/games`：讀取 session 內所有遊戲
- `POST /api/switch_game`：切換目前遊戲

### Python 呼叫 C 引擎命令

- `init <state_file> <rows> <cols> <mines>`
- `reveal <state_file> <r> <c>`
- `flag <state_file> <r> <c>`
- `status <state_file>`

### 回傳 JSON 重點欄位

- 基本：`ok`, `rows`, `cols`, `mines`, `revealed`, `game_over`, `won`, `board`
- 擴充：`elapsed_seconds`, `best_record_seconds`, `active_game_id`, `history`

## 7) 教學範例用途

本專題可作為以下課程教學範例：

- **C 與 Python 跨語言整合**
- **資料結構應用**
  - Stack：Undo
  - Linked List：操作歷史序列
- **檔案持久化與狀態管理**
- **Web API 與前後端分離架構**
- **session-based 多使用情境（多盤切換）**

可延伸方向：排行榜、帳號系統、多人即時對戰（WebSocket）、部署到雲端。
