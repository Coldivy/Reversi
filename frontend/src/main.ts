import { BoardView } from "./ui/board";
import { GameEngine } from "./core/game";
import { MessageView } from "./ui/msg";
import { fetchAIMove } from "./api/ai";

// 初始化引擎和 UI 组件
const engine = new GameEngine();
const msgView = new MessageView("game-status", "game-score");

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
    isGameOver: engine.isGameOver(),
    winner: engine.getWinner(), // "Black", "White", "Draw" 或 null
  });
}

// ==========================================
// 模式 A: 双人对弈 (人类 vs 人类)
// ==========================================
async function handleHumanVsHuman(r: number, c: number) {
  const success = engine.makeMove(r, c);
  if (success) {
    syncUI();
  }
}

// ==========================================
// 模式 B: 人机对弈 (人类 vs Python AI)
// ==========================================

async function handleHumanVsAI(r: number, c: number) {
  // 1. 人类落子（假设人类永远是黑棋 Player）
  if (engine.currentPlayer !== "Player") return;

  const playerSuccess = engine.makeMove(r, c);
  if (!playerSuccess) return; // 非法落子

  syncUI(); // 渲染人类落子后的状态

  // 如果游戏没结束且轮到 AI
  const nextPlayer = engine.currentPlayer as "Player" | "AI";
  if (!engine.isGameOver() && nextPlayer === "AI") {
    try {
      // 2. 调用 AI 接口
      const aiMove = await fetchAIMove(engine.getGrid(), -1); // -1 代表白棋

      // 模拟 AI 思考延迟，提升体验
      // await new Promise(resolve => setTimeout(resolve, 600));

      // 3. AI 落子
      engine.makeMove(aiMove.r, aiMove.c);
      syncUI();
    } catch (error) {
      console.error("AI 获取落子失败:", error);
    }
  }
}

// 初始化棋盘视图
// --- 在这里切换模式：将 handleHumanVsAI 换成 handleHumanVsHuman 即可切换 ---
// const view = new BoardView("board", handleHumanVsAI);
const view = new BoardView("board", handleHumanVsHuman);

// 首次渲染（显示初始四子和初始比分）
syncUI();
