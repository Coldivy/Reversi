import { BoardView } from "./ui/board";
import { MessageView } from "./ui/msg";
import { showChoosePanel } from "./ui/choose";
import { GameEngine } from "./core/game";
import type { PlayerValue } from "./core/game";
import { fetchAIMove } from "./api/ai";

// 定义游戏状态
type GameStep = "choose" | "playing" | "end";

// 玩家颜色
let aiPlayerValue: PlayerValue;
let playerValue: PlayerValue;

// 初始化引擎和 UI 组件
let engine: GameEngine;
let view: BoardView;
const msgView = new MessageView("game-status", "game-score");

// 预防 Vite HMR 重复绑定：如果已经有实例，先清空或刷新
const boardContainer = document.getElementById("board")!;
const newBoard = boardContainer.cloneNode(true);
boardContainer.parentNode!.replaceChild(newBoard, boardContainer);

// 全局锁
let isProcessing = false;

// 游戏状态
let step: GameStep = "choose";

/**
 * 统一同步 UI 的函数
 */
function syncUI() {
  // 更新棋盘 DOM
  view.update(engine.getGrid());

  const score = engine.getScore();

  // 更新文字信息
  msgView.render({
    blackScore: score.black,
    whiteScore: score.white,
    currentPlayerName: engine.currentPlayer,
    currentPlayerValue: engine._player,
    isGameOver: engine.isGameOver(),
    winner: engine.getWinner(), // "Black", "White", "Draw" 或 null
  });
}

// AI 思考和落子逻辑
async function runAIProcess() {
  if (isProcessing) return;

  try {
    isProcessing = true;

    // 只要游戏没结束，且当前轮到 AI，就持续运行
    while (!engine.isGameOver() && (engine.currentPlayer as string) === "AI") {
      console.log("%c轮到 AI 思考...", "color: blue; font-weight: bold;");

      const currentGrid = JSON.parse(JSON.stringify(engine.getGrid()));
      const aiMove = await fetchAIMove(currentGrid, aiPlayerValue);

      if (aiMove.r === -1) {
        // engine.makeMove 内部会处理跳过，但为了安全这里 break
        break;
      }

      await new Promise((resolve) => setTimeout(resolve, 600)); // 视觉延迟

      const aiSuccess = engine.makeMove(aiMove.r, aiMove.c);
      if (!aiSuccess) break;

      syncUI();
    }
  } catch (error) {
    console.error("AI 执行出错:", error);
  } finally {
    isProcessing = false;
    syncUI();
  }
}

// 模式 A: 双人对弈 (人类 vs 人类)
// @ts-ignore
async function handleHumanVsHuman(r: number, c: number) {
  const success = engine.makeMove(r, c);
  if (success) {
    syncUI();
  }
}

// 模式 B: 人机对弈 (人类 vs Python AI)
async function handleHumanVsAI(r: number, c: number) {
  if (step !== "playing" || isProcessing) return;
  if (engine.currentPlayer !== "Player") return;

  // 1. 玩家落子
  const playerSuccess = engine.makeMove(r, c);
  if (!playerSuccess) return;

  syncUI();

  // 2. 玩家落子结束后，触发 AI
  await runAIProcess();
}

// 选择面板与初始化
showChoosePanel((selectedColor) => {
  playerValue = selectedColor as PlayerValue;
  aiPlayerValue = -selectedColor as PlayerValue;

  engine = new GameEngine(playerValue); // 这里内部现在固定黑棋先手了
  view = new BoardView("board", handleHumanVsAI);

  step = "playing";
  syncUI();

  // 检查如果首个回合就是 AI (玩家选了白棋)，则立即启动 AI ---
  if (engine.currentPlayer === "AI") {
    runAIProcess();
  }
});
