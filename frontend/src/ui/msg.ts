import type { PlayerValue } from "../core/game";

export interface GameStatus {
  blackScore: number;
  whiteScore: number;
  currentPlayerName: string;
  currentPlayerValue: PlayerValue;
  isGameOver: boolean;
  winner: string | null;
}

export class MessageView {
  private statusElement: HTMLElement;
  private scoreElement: HTMLElement;

  constructor(statusId: string, scoreId: string) {
    this.statusElement = document.getElementById(statusId) as HTMLElement;
    this.scoreElement = document.getElementById(scoreId) as HTMLElement;
  }

  // 核心方法：根据传入的状态更新 UI
  public render(status: GameStatus) {
    // 1. 更新分数显示
    this.scoreElement.innerHTML = `<span class = "score-black">黑棋: ${status.blackScore}</span> &nbsp;  &nbsp; <span class="score-white">白棋: ${status.whiteScore}</span>`;

    // 2. 更新状态显示
    if (status.isGameOver) {
      this.statusElement.classList.add("game-over");
      if (status.winner === "Draw") {
        this.statusElement.innerText = "游戏结束：平局！";
      } else {
        this.statusElement.innerText = `游戏结束：${status.winner === "Black" ? "黑棋" : "白棋"} 获胜！`;
      }
    } else {
      this.statusElement.classList.remove("game-over");
      this.statusElement.innerHTML = `当前回合: <strong>${status.currentPlayerName === "Player" ? "人类" : "AI/对手 "}${status.currentPlayerValue === 1 ? "（黑）" : "（白）"}</strong>`;
    }
  }

  // 显示临时通知的方法（比如：“无处落子，跳过回合”）
  public static flashNotice(text: string) {
    const notice = document.createElement("div");
    notice.className = "notice-popup";
    notice.innerText = text;
    document.body.appendChild(notice);

    // 停留一段时间后开始淡出
    setTimeout(() => {
      notice.classList.add("hide");

      // 监听过渡动画结束事件
      notice.addEventListener(
        "transitionend",
        () => {
          notice.remove();
        },
        { once: true },
      ); // { once: true } 确保监听器只执行一次
    }, 1000);
  }
}
