import { BoardView } from "./ui/board";
import { GameEngine } from "./core/game";
import { fetchAIMove } from "./api/ai";

// 1. 初始化引擎
const engine = new GameEngine();

// 2. 初始化界面
const view = new BoardView("board", async (r, c) => {
  // 当点击发生时的逻辑流程：

  // A. 检查是否合法
  if (!engine.isValidMove(r, c)) return;

  // B. 玩家落子并更新逻辑状态
  engine.makeMove(r, c);

  // C. 更新 UI
  view.update(engine.getGrid());

  // D. 轮到 AI
  if (engine.currentPlayer === "AI") {
    const aiMove = await fetchAIMove(engine.getGrid());
    engine.makeMove(aiMove.r, aiMove.c);
    view.update(engine.getGrid());
  }
});

// 首次渲染
view.update(engine.getGrid());
