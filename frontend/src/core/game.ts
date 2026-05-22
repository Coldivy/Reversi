import { MessageView } from "../ui/msg";

// 定义类型
export type PlayerValue = 1 | -1;
export type PlayerName = "Player" | "AI";

// 定义返回结果的接口
export interface GameScore {
  black: number;
  white: number;
  empty: number;
}

export class GameEngine {
  private _player: PlayerValue;
  private initialPlayer: PlayerValue;
  private currentGrid: number[][];

  constructor(player: PlayerValue) {
    // 初始化棋盘，黑为1，白为-1
    this.currentGrid = Array.from({ length: 8 }, () => Array(8).fill(0));
    this.currentGrid[3][3] = this.currentGrid[4][4] = -1;
    this.currentGrid[3][4] = this.currentGrid[4][3] = 1;
    this._player = 1; // 黑子先手
    this.initialPlayer = player;
  }

  public isValidMove(
    r: number,
    c: number,
    player: PlayerValue = this._player,
  ): boolean {
    if (this.currentGrid[r][c] != 0) return false;
    return this.getFlippablePieces(r, c, player).length > 0;
  }

  // 获取可翻转的棋子
  private getFlippablePieces(
    r: number,
    c: number,
    player: PlayerValue,
  ): { r: number; c: number }[] {
    const flippable = [];
    const directions = [
      [-1, 0],
      [1, 0],
      [0, -1],
      [0, 1],
      [-1, -1],
      [-1, 1],
      [1, 1],
      [1, -1],
    ];

    for (const [dr, dc] of directions) {
      let temp = [];
      let currR = r + dr;
      let currC = c + dc;

      while (
        currC >= 0 &&
        currC <= 7 &&
        currR >= 0 &&
        currR <= 7 &&
        this.currentGrid[currR][currC] === -player
      ) {
        temp.push({ r: currR, c: currC });
        currR = currR + dr;
        currC = currC + dc;
      }

      if (
        currR >= 0 &&
        currR <= 7 &&
        currC >= 0 &&
        currC <= 7 &&
        this.currentGrid[currR][currC] === player
      ) {
        if (temp.length > 0) flippable.push(...temp);
      }
    }

    return flippable;
  }

  public getGrid(): number[][] {
    return this.currentGrid;
  }

  private switch() {
    this._player = -this._player as PlayerValue;
  }

  public get currentPlayer(): PlayerName {
    return this._player === this.initialPlayer ? "Player" : "AI";
  }

  public get currentPlayerValue(): PlayerValue {
    return this._player;
  }

  public makeMove(r: number, c: number): boolean {
    if (!this.isValidMove(r, c, this._player)) return false;

    this.getFlippablePieces(r, c, this._player).forEach((p) => {
      this.currentGrid[p.r][p.c] = this._player;
    });

    this.currentGrid[r][c] = this._player;
    this.switch();

    // 处理特殊情况：跳过回合 或 游戏结束
    if (!this.canMove(this._player)) {
      if (this.isGameOver()) {
        console.log("游戏结束！双方均无合法落子。");
        MessageView.flashNotice("游戏结束！双方均无合法落子");
      } else {
        console.log(`${this._player} 无处落子，跳过回合。`);
        MessageView.flashNotice("无处落子，跳过回合");
        this.switch(); // 自动切回当前落子方
      }
    }

    return true;
  }

  // 辅助方法：检查当前玩家是否有棋可下
  public canMove(player: PlayerValue): boolean {
    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        if (this.isValidMove(r, c, player)) return true;
      }
    }
    return false;
  }

  public isGameOver(): boolean {
    // 游戏结束：双方都无处落子
    return !this.canMove(1) && !this.canMove(-1);
  }

  public getScore() {
    return this.currentGrid.flat().reduce(
      (acc, cell) => {
        if (cell === 1) acc.black++;
        else if (cell === -1) acc.white++;
        else acc.empty++;
        return acc;
      },
      { black: 0, white: 0, empty: 0 }, // 初始值
    );
  }

  public getWinner(): "Black" | "White" | "Draw" | null {
    if (!this.isGameOver()) return null;

    const score = this.getScore();
    if (score.black > score.white) return "Black";
    if (score.white > score.black) return "White";
    return "Draw";
  }
}
