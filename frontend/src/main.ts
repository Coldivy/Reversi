import { BoardView } from "./ui/board";
import { MessageView } from "./ui/msg";
import { showChoosePanel } from "./ui/choose";
import { GameEngine } from "./core/game";
import { fetchAIMove, fetchResetAI } from "./api/ai";

// 定义游戏状态
type GameStep = "choose" | "playing" | "end";

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

// API 地址（若为空字符串，代表手动）
let blackApiUrl = "";
let whiteApiUrl = "";

// 统一同步 UI 的函数
function syncUI() {
  // 更新棋盘 DOM
  view.update(engine.getGrid());

  const score = engine.getScore();
  const currentTurn = engine.currentPlayerValue;

  // 根据当前回合对应的 URL 是否存在，决定显示的名称
  const currentUrl = currentTurn === 1 ? blackApiUrl : whiteApiUrl;
  const roleName = currentUrl ? "AI" : "Player";

  // 更新文字信息
  msgView.render({
    blackScore: score.black,
    whiteScore: score.white,
    currentPlayerName: roleName,
    currentPlayerValue: currentTurn,
    isGameOver: engine.isGameOver(),
    winner: engine.getWinner(), // "Black", "White", "Draw" 或 null
  });
}

// 核心回合调度器：根据当前棋盘状态决定下一步是由 AI 还是玩家操作
function nextTurn() {
  if (engine.isGameOver()) {
    step = "end";
    syncUI();
    return;
  }

  const currentTurn = engine.currentPlayerValue;
  const currentUrl = currentTurn === 1 ? blackApiUrl : whiteApiUrl;

  if (currentUrl) {
    // 如果当前角色有 API 链接，自动进入 AI 执行逻辑
    executeAIMove(currentUrl, currentTurn);
  } else {
    // 如果没有，等待浏览器中的玩家手动下棋
    console.log("等待玩家手动落子...");
  }
}

// AI 思考和落子逻辑
async function executeAIMove(apiUrl: string, playerValue: 1 | -1) {
  if (step !== "playing") return;
  if (isProcessing) return;
  isProcessing = true;

  try {
    console.log(
      `%c轮到 AI (${playerValue === 1 ? "黑棋" : "白棋"}) 思考...`,
      "color: blue; font-weight: bold;",
    );

    const currentGrid = JSON.parse(JSON.stringify(engine.getGrid()));
    const aiMove = await fetchAIMove(apiUrl, currentGrid, playerValue);

    if (aiMove.r !== -1) {
      await new Promise((resolve) => setTimeout(resolve, 600)); // 视觉延迟
      const aiSuccess = engine.makeMove(aiMove.r, aiMove.c);
      if (aiSuccess) {
        syncUI();
      }
    }
  } catch (error) {
    console.error("AI落子执行出错:", error);
  } finally {
    isProcessing = false;
    // 无论当前 AI 步骤是否成功，都要触发下一次调度
    nextTurn();
  }
}

// 玩家手动落子函数
function executePlayerMove(r: number, c: number) {
  if (step !== "playing") return;
  if (isProcessing) return;
  if (engine.isGameOver()) return;

  const currentTurn = engine.currentPlayerValue;
  const currentUrl = currentTurn === 1 ? blackApiUrl : whiteApiUrl;

  // 如果配置了 API，说明这方应由 AI 控制，拦截手动点击
  if (currentUrl) {
    return;
  }

  const success = engine.makeMove(r, c);
  if (success) {
    syncUI();
    // 玩家落子成功后，进入下个回合的调度（可能是另一个玩家，或是 AI）
    nextTurn();
  }
}

// 选择面板与初始化
showChoosePanel((blackUrl, whiteUrl) => {
  blackApiUrl = blackUrl;
  whiteApiUrl = whiteUrl;
  step = "playing";

  // 新局开始前，通知配置了 API 链接的后端重置置换表
  if (blackApiUrl) {
    fetchResetAI(blackApiUrl).catch((err) =>
      console.error("重置黑棋AI失败:", err),
    );
  }
  // 如果白棋是另一个后端 AI 链接，也同样通知重置
  if (whiteApiUrl && whiteApiUrl !== blackApiUrl) {
    fetchResetAI(whiteApiUrl).catch((err) =>
      console.error("重置白棋AI失败:", err),
    );
  }

  engine = new GameEngine(1); // 默认黑棋先手

  // 绑定玩家落子的点击事件
  view = new BoardView("board", (r: number, c: number) => {
    executePlayerMove(r, c);
  });

  syncUI();

  // 开启游戏第一回合
  nextTurn();
});
