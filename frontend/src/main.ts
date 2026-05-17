import { BoardView } from "./ui/board";
import { GameEngine } from "./core/game";
import { MessageView } from "./ui/msg";
import { fetchAIMove } from "./api/ai";

// 初始化引擎和 UI 组件
const engine = new GameEngine();
const msgView = new MessageView("game-status", "game-score");

// 预防 Vite HMR 重复绑定：如果已经有实例，先清空或刷新
const boardContainer = document.getElementById("board")!;
const newBoard = boardContainer.cloneNode(true);
boardContainer.parentNode!.replaceChild(newBoard, boardContainer);

// 唯一的全局锁
let isProcessing = false;

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
// @ts-ignore
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
  // 1. 瞬间拦截：第一行就检查锁
  if (isProcessing) return;

  // 2. 身份检查：不是人类回合不准动
  if (engine.currentPlayer !== "Player") return;

  try {
    isProcessing = true; // 锁定

    // 3. 玩家落子
    console.log(`玩家尝试落子: [${r}, ${c}]`);
    const playerSuccess = engine.makeMove(r, c);
    if (!playerSuccess) {
      console.warn("无效的人类落子");
      isProcessing = false;
      return;
    }

    syncUI();

    // 4. 进入 AI 自动化处理循环
    // 只要游戏没结束，且轮到 AI，就一直跑（处理 AI 连走的情况）

    while (!engine.isGameOver() && (engine.currentPlayer as string) === "AI") {
      console.log("%c轮到 AI 思考...", "color: blue; font-weight: bold;");

      // 获取当前棋盘状态副本，防止引用冲突
      const currentGrid = JSON.parse(JSON.stringify(engine.getGrid()));

      const aiMove = await fetchAIMove(currentGrid, -1);
      console.log(`AI 返回坐标: [${aiMove.r}, ${aiMove.c}]`);

      if (aiMove.r === -1) {
        console.log("AI 无棋可走，自动跳过");
        // 实际上 engine.makeMove 内部已处理跳过，
        // 这里 break 是为了防止死循环
        break;
      }

      // 视觉延迟
      await new Promise((resolve) => setTimeout(resolve, 600));

      // 执行 AI 落子
      const aiSuccess = engine.makeMove(aiMove.r, aiMove.c);

      if (!aiSuccess) {
        // 如果这里报错，说明后端 AI 算的棋步在前端 Engine 看来是违规的
        // 或者是由于并发导致该位置已被占
        console.error("%c逻辑冲突：AI 返回了非法坐标", "color: red", aiMove);
        break;
      }

      syncUI();
    }
  } catch (error) {
    console.error("执行过程出错:", error);
  } finally {
    isProcessing = false; // 最终必须解锁
    console.log("流程结束，解锁");
    syncUI();
  }
}

// 初始化棋盘视图
// --- 在这里切换模式：将 handleHumanVsAI 换成 handleHumanVsHuman 即可切换 ---
const view = new BoardView("board", handleHumanVsAI);
// const view = new BoardView("board", handleHumanVsHuman);

// 首次渲染（显示初始四子和初始比分）
syncUI();
