import { BoardView } from "./ui/board";
import { MessageView } from "./ui/msg";
import { showChoosePanel } from "./ui/choose";
import { GameEngine } from "./core/game";
import { fetchAIMove, fetchResetAI } from "./api/ai";
import type { EngineConfig } from "./api/ai";

// ======================================================================
//  状态
// ======================================================================

type GameStep = "choose" | "playing" | "end";

let engine: GameEngine;
let view: BoardView;
const msgView = new MessageView("game-status", "game-score");

/* 防止 Vite HMR 重复绑定 */
const boardContainer = document.getElementById("board")!;
const newBoard = boardContainer.cloneNode(true);
boardContainer.parentNode!.replaceChild(newBoard, boardContainer);

let isProcessing = false;
let step: GameStep = "choose";

/** 每方的引擎配置（null = 人类玩家） */
let blackConfig: EngineConfig | null = null;
let whiteConfig: EngineConfig | null = null;

// ======================================================================
//  UI 同步
// ======================================================================

function syncUI() {
  view.update(engine.getGrid());

  const score = engine.getScore();
  const currentTurn = engine.currentPlayerValue;

  const currentConfig = currentTurn === 1 ? blackConfig : whiteConfig;
  const roleName = currentConfig ? "AI" : "Player";

  msgView.render({
    blackScore: score.black,
    whiteScore: score.white,
    currentPlayerName: roleName,
    currentPlayerValue: currentTurn,
    isGameOver: engine.isGameOver(),
    winner: engine.getWinner(),
  });
}

// ======================================================================
//  回合调度
// ======================================================================

function nextTurn() {
  if (engine.isGameOver()) {
    step = "end";
    syncUI();
    return;
  }

  const currentTurn = engine.currentPlayerValue;
  const currentConfig = currentTurn === 1 ? blackConfig : whiteConfig;

  if (currentConfig) {
    executeAIMove(currentConfig, currentTurn);
  } else {
    console.log("等待玩家手动落子...");
  }
}

// ======================================================================
//  AI 落子
// ======================================================================

async function executeAIMove(config: EngineConfig, playerValue: 1 | -1) {
  if (step !== "playing") return;
  if (isProcessing) return;
  isProcessing = true;

  try {
    const label = playerValue === 1 ? "黑棋" : "白棋";
    console.log(
      `%c轮到 AI [${config.engine}] (${label}) 思考...`,
      "color: blue; font-weight: bold;",
    );

    const currentGrid = JSON.parse(JSON.stringify(engine.getGrid()));
    const aiMove = await fetchAIMove(config, currentGrid, playerValue);

    if (aiMove.r !== -1) {
      const aiSuccess = engine.makeMove(aiMove.r, aiMove.c);
      if (aiSuccess) {
        syncUI();
      }
    }
  } catch (error) {
    console.error("AI 落子执行出错:", error);
  } finally {
    isProcessing = false;
    nextTurn();
  }
}

// ======================================================================
//  玩家落子
// ======================================================================

function executePlayerMove(r: number, c: number) {
  if (step !== "playing") return;
  if (isProcessing) return;
  if (engine.isGameOver()) return;

  const currentTurn = engine.currentPlayerValue;
  const currentConfig = currentTurn === 1 ? blackConfig : whiteConfig;

  /* 如果该方配置了 AI 引擎，拦截手动点击 */
  if (currentConfig) return;

  const success = engine.makeMove(r, c);
  if (success) {
    syncUI();
    nextTurn();
  }
}

// ======================================================================
//  启动 → 选择面板
// ======================================================================

showChoosePanel(async (bc, wc) => {
  blackConfig = bc;
  whiteConfig = wc;
  step = "playing";

  /* 新局前重置各 AI 后端 */
  if (blackConfig) {
    try {
      await fetchResetAI(blackConfig);
    } catch (err) {
      console.error("重置黑棋 AI 失败:", err);
    }
  }
  if (whiteConfig && whiteConfig.url !== blackConfig?.url) {
    try {
      await fetchResetAI(whiteConfig);
    } catch (err) {
      console.error("重置白棋 AI 失败:", err);
    }
  }

  engine = new GameEngine(1);

  view = new BoardView("board", (r: number, c: number) => {
    executePlayerMove(r, c);
  });

  syncUI();
  nextTurn();
});
