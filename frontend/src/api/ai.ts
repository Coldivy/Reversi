// 定义坐标类型
export interface Point {
  r: number;
  c: number;
}

/**
 * 向 Python 后端请求 AI 的下一步落子
 * @param apiUrl 当前AI接口地址
 * @param grid 当前的 8x8 棋盘数组
 * @param aiPlayerValue AI 的玩家值（默认 -1 代表白棋）
 * @returns 返回一个 Promise，解析为 AI 决定的坐标 {r, c}
 */
export async function fetchAIMove(
  apiUrl: string,
  grid: number[][],
  aiPlayerValue: number,
): Promise<Point> {
  try {
    // 发起 POST 请求
    const response = await fetch(apiUrl, {
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

/**
 * 向 Python 后端请求 AI 的下一步落子（限时搜索版本）
 * @param apiUrl 当前AI接口地址（如 http://127.0.0.1:8000/api/ai-move-timelimit）
 * @param grid 当前的 8x8 棋盘数组
 * @param aiPlayerValue AI 的玩家值（默认 -1 代表白棋）
 * @param timeLimitMs 时间上限（毫秒）
 * @returns 返回一个 Promise，解析为 AI 决定的坐标 {r, c, depth}
 */
export async function fetchAIMoveTimed(
  apiUrl: string,
  grid: number[][],
  aiPlayerValue: number,
  timeLimitMs: number,
): Promise<{ r: number; c: number; depth: number }> {
  try {
    const response = await fetch(apiUrl, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        board: grid,
        player: aiPlayerValue,
        time_limit_ms: timeLimitMs,
      }),
    });

    if (!response.ok) {
      throw new Error(`后端请求失败，状态码: ${response.status}`);
    }

    const data = await response.json();

    if (typeof data.r !== "number" || typeof data.c !== "number") {
      throw new Error("后端返回的数据格式不正确，期望 {r, c}");
    }

    return { r: data.r, c: data.c, depth: data.depth ?? 0 };
  } catch (error) {
    console.error("AI 限时搜索接口通信报错:", error);
    throw error;
  }
}

/**
 * 告知后端重置 AI 引擎（清空上一局置换表）
 * @param apiUrl 当前AI接口地址（例如：http://127.0.0.1:8000/api）
 */
export async function fetchResetAI(apiUrl: string): Promise<void> {
  try {
    // 将 "/ai-move" 或 "/ai-move-timelimit" 替换为 "/init"
    const initUrl = apiUrl
      .replace("/ai-move-timelimit", "/init")
      .replace("/ai-move", "/init");

    const response = await fetch(initUrl, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
    });

    if (!response.ok) {
      console.warn("通知 AI 引擎重置失败，可能未实现重置接口");
    } else {
      console.log("AI 引擎重置成功，置换表已清空");
    }
  } catch (error) {
    console.error("重置 AI 引擎通信异常:", error);
  }
}
