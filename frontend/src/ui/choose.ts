export function showChoosePanel(onChoose: (playerColor: 1 | -1) => void) {
  const panel = document.createElement("div");
  panel.id = "choose-panel";
  panel.innerHTML = `
<p>请选择玩家（人类）棋子颜色<p>
  <div id = "btn-black">黑</div>
  <div id = "btn-white">白</div>
`;

  const overlay = document.createElement("div");
  overlay.id = "choose-overlay";
  document.body.appendChild(overlay);
  overlay.appendChild(panel);

  const btnBlack = document.getElementById("btn-black") as HTMLDivElement;
  const btnWhite = document.getElementById("btn-white") as HTMLDivElement;

  btnBlack.addEventListener("click", () => {
    cleanup();
    onChoose(1);
  });
  btnWhite.addEventListener("click", () => {
    cleanup();
    onChoose(-1);
  });

  function cleanup() {
    overlay.remove();
  }
}
