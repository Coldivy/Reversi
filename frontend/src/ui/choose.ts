export function showChoosePanel(
  onChoose: (blackUrl: string, whiteUrl: string) => void,
) {
  const panel = document.createElement("div");
  panel.id = "choose-panel";
  panel.innerHTML = `
<p>请选择玩家<p>
  <form id = "black-input" class = "form">
    <input type = "url" id = "black-url" class = "form-input">
    <label for = "black-url" class = "form-label">
      Black's api url
    </label>
  </form>
  <form id = "white-input" class = "form">
    <input type = "url" id = "white-url" class = "form-input">
    <label for ="white-url" class = "form-label">
      White's api url
    </label>
  </form>
  <div id = "start-btn">开始</div>
  <br>
  <div id = "remarks">留空为人类玩家手动点击<br>内置AI输入"/api/ai-move"<br>固定时间搜索AI输入"/api/ai-move-timelimit"（默认控制在3秒内）<br>注意：为了节省内存，简洁化代码，内置AI使用同一置换表。<br><b>所以不要同时让深度固定AI与限时AI进行对战</b>，会污染置换表导致走法变形，无法反映真实强度！</div>
`;

  const overlay = document.createElement("div");
  overlay.id = "choose-overlay";
  document.body.appendChild(overlay);
  overlay.appendChild(panel);

  const startBtn = document.getElementById("start-btn") as HTMLDivElement;
  const blackInput = document.getElementById("black-url") as HTMLInputElement;
  const whiteInput = document.getElementById("white-url") as HTMLInputElement;

  startBtn.addEventListener("click", () => {
    const blackUrl = blackInput.value.trim();
    const whiteUrl = whiteInput.value.trim();
    onChoose(blackUrl, whiteUrl);
    cleanup();
  });

  function cleanup() {
    overlay.remove();
  }
}
