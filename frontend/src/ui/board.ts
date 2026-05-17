export class BoardView {
  private boardElement: HTMLDivElement;
  private onCellClickCallback: (row: number, col: number) => void;

  constructor(
    containerId: string,
    onCellClick: (row: number, col: number) => void,
  ) {
    this.boardElement = document.getElementById(containerId) as HTMLDivElement;
    this.onCellClickCallback = onCellClick;
    this.init();
  }

  private init() {
    // 1. 定义数据结构
    const cells = Array.from({ length: 64 }, (_, i) => ({
      row: Math.floor(i / 8),
      col: i % 8,
    }));

    // 2. 创造片段
    const fragment = document.createDocumentFragment();

    cells.forEach(({ row, col }) => {
      const cell = document.createElement("div");
      const colorClass = (row + col) % 2 === 0 ? "cell-dark" : "cell-light";
      cell.className = `cell ${colorClass}`;
      cell.dataset.row = row.toString();
      cell.dataset.col = col.toString();
      cell.id = `cell-${row}-${col}`;
      fragment.appendChild(cell);
    });

    this.boardElement.appendChild(fragment);

    // 3. 添加事件监听
    this.boardElement.addEventListener("click", (e: MouseEvent) => {
      if (!(e.target instanceof HTMLElement)) return;
      const cell = e.target.closest(".cell");
      if (cell instanceof HTMLElement) {
        const row = parseInt(cell.dataset.row ?? "0", 10);
        const col = parseInt(cell.dataset.col ?? "0", 10);
        // 触发回调，交给逻辑层处理
        this.onCellClickCallback(row, col);
      }
    });
  }

  // 暴露给外部的方法：根据棋盘数据更新 DOM
  public update(grid: number[][]) {
    for (let r = 0; r < 8; r++) {
      for (let c = 0; c < 8; c++) {
        const cell = document.getElementById(`cell-${r}-${c}`);
        if (!cell) continue;

        // 清空格子内容
        cell.innerHTML = "";

        // 如果有棋子，创建棋子 DOM
        if (grid[r][c] !== 0) {
          const piece = document.createElement("div");
          piece.className = `piece_${grid[r][c] === 1 ? "black" : "white"}`;
          cell.appendChild(piece);
        }
      }
    }
  }
}
