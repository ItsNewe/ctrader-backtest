"""
FastAPI application for the Trading Analysis Dashboard.
Replaces the Flask server.py with async support and native WebSocket.
"""

import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from api.config import get_settings
from api.routers import broker, backtest, strategies, data, sweep, browse, analysis, commissions, versions

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
)
logger = logging.getLogger(__name__)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application startup/shutdown."""
    settings = get_settings()
    logger.info(f"Starting Trading Dashboard API")
    logger.info(f"Project root: {settings.project_root}")
    logger.info(f"Build dir: {settings.build_dir}")
    logger.info(f"Data dir: {settings.data_dir}")
    logger.info(f"Backtest CLI: {settings.backtest_exe_path}")

    # Ensure results directory exists
    settings.results_dir.mkdir(exist_ok=True)

    yield

    logger.info("Shutting down Trading Dashboard API")


app = FastAPI(
    title="Trading Analysis Dashboard API",
    description="Backend for the backtesting dashboard. Connects to MT5, runs C++ backtests, manages parameter sweeps.",
    version="1.0.0",
    lifespan=lifespan,
)

# CORS - allow React dev server
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://127.0.0.1:5173"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Register routers
app.include_router(broker.router)
app.include_router(backtest.router)
app.include_router(strategies.router)
app.include_router(data.router)
app.include_router(sweep.router)
app.include_router(browse.router)
app.include_router(analysis.router)
app.include_router(commissions.router)
app.include_router(versions.router)


# Health check
@app.get("/api/health")
async def health_check():
    settings = get_settings()
    return {
        "status": "ok",
        "backtest_cli_exists": settings.backtest_exe_path.exists(),
        "data_dir_exists": settings.data_dir.exists(),
    }


# Serve React production build (if it exists)
dashboard_build = Path(__file__).parent.parent / "dashboard" / "dist"
if dashboard_build.exists():
    app.mount("/", StaticFiles(directory=str(dashboard_build), html=True), name="dashboard")


if __name__ == "__main__":
    import uvicorn

    settings = get_settings()
    uvicorn.run(
        "api.main:app",
        host=settings.host,
        port=settings.port,
        reload=settings.debug,
    )
