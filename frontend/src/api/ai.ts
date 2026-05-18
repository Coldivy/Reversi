// 定义坐标类型
export interface Point {
  r: number;
  c: number;
}

// Python 后端地址 (如果是本地开发，通常是 5000 或 8000 端口)
const AI_API_URL = "/api/ai-move";

/**
 * 向 Python 后端请求 AI 的下一步落子
 * @param grid 当前的 8x8 棋盘数组
 * @param aiPlayerValue AI 的玩家值（默认 -1 代表白棋）
 * @returns 返回一个 Promise，解析为 AI 决定的坐标 {r, c}
 */
export async function fetchAIMove(
  grid: number[][],
  aiPlayerValue: number,
): Promise<Point> {
  try {
    // 发起 POST 请求
    const response = await fetch(AI_API_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      // 将棋盘和 AI 身份转为 JSON 字符串发送
      body: JSON.stringify({
        board: grid,
        player: aiPlayerValue,
      }),
    });

    // 检查 HTTP 状态码
    if (!response.ok) {
      throw new Error(`后端请求失败，状态码: ${response.status}`);
    }

    // 解析后端返回的 JSON 数据
    const data = await response.json();

    // 校验返回的数据格式是否包含 r 和 c
    if (typeof data.r !== "number" || typeof data.c !== "number") {
      throw new Error("后端返回的数据格式不正确，期望 {r, c}");
    }

    return { r: data.r, c: data.c };
  } catch (error) {
    console.error("AI 接口通信报错:", error);
    // 把错误抛给 main.ts，让上层处理
    throw error;
  }
}
