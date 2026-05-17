from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from api.routes import router as game_router

app = FastAPI(title="Othello AI Backend")

# 1. 配置跨域 (CORS)
app.add_middleware(
    CORSMiddleware,  # type: ignore
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# 2. 注册路由
# prefix="/api" 会让你的接口地址变成 http://localhost:8000/api/ai-move
app.include_router(game_router, prefix="/api")


@app.get("/")
async def root():
    return {"message": "Reversi AI Server is running"}
