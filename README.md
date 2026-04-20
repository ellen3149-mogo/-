# Flask + C 踩地雷專案

此專案示範：
- **前端/控制層**：Python Flask
- **後端遊戲引擎**：C 語言（負責地雷資料、翻格、插旗、勝負判定）

## 環境需求

- Python 3.10+
- GCC（Windows 可用 MinGW）

## 安裝與啟動

1. 安裝 Python 套件
   ```bash
   pip install -r requirements.txt
   ```

2. 啟動 Flask
   ```bash
   python app.py
   ```

3. 開啟瀏覽器
   - [http://127.0.0.1:5000](http://127.0.0.1:5000)

> 第一次啟動時，`app.py` 會自動編譯 `backend/minesweeper_engine.c` 成 `backend/minesweeper_engine.exe`。

## 架構說明

- `app.py`：Flask 路由與畫面渲染，透過 `subprocess` 呼叫 C 後端
- `backend/minesweeper_engine.c`：遊戲邏輯與狀態儲存
- `templates/index.html`：主畫面
- `static/style.css`：樣式
- `runtime/*.ms`：每場遊戲狀態檔（binary）

## 目前功能

- 可自訂列數、行數、地雷數
- 左鍵邏輯（Reveal）
- 插旗/取消插旗（Flag）
- 自動展開空白區塊
- 踩雷失敗與全部安全格翻開即勝利判定

## 依 PDF 規範客製化

你提供的 PDF 目前看起來是掃描版，這邊無法直接擷取文字。  
請再提供 PDF 規範重點（或拍幾張清楚截圖），我可以立刻把：
- 畫面規格
- 難度模式
- 計時/分數
- 報告所需檔案格式
調整成完全對應你課程要求的版本。
