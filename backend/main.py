from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from api.routes import router as game_router
from fastapi.responses import FileResponse
import os
import webbrowser
from threading import Timer
import uvicorn

app = FastAPI(title="Reversi AI Backend")

# ── CORS 中间件（允许来自任意来源的前端请求） ──
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# 注册路由
app.include_router(game_router, prefix="/api")

# 挂载静态文件目录
# 注意：html=True 会自动寻找目录下的 index.html
app.mount("/static", StaticFiles(directory="static"), name="static")


# 定义根路径返回前端页面
@app.get("/")
async def read_index():
    return FileResponse(os.path.join("static", "index.html"))


def open_browser():
    # 延迟1.5秒打开，确保服务器已经启动完成
    webbrowser.open("http://127.0.0.1:8000")


if __name__ == "__main__":
    # 启动一个计时器，在 1.5 秒后自动打开浏览器
    Timer(1.5, open_browser).start()

    # 启动服务器
    uvicorn.run(app, host="127.0.0.1", port=8000)
