import os
import shutil
import tempfile
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response

if shutil.which("ffmpeg") is None:
    _ffmpeg_dir = r"C:\Users\prissetik\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1.2-full_build\bin"
    if os.path.isdir(_ffmpeg_dir):
        os.environ["PATH"] = _ffmpeg_dir + os.pathsep + os.environ["PATH"]

from rvc_python.infer import RVCInference  # noqa: E402

MODELS_DIR = str(Path(__file__).parent / "models")
DEVICE = os.environ.get("RVC_DEVICE", "cuda:0")
DEFAULT_MODEL = os.environ.get("RVC_DEFAULT_MODEL", "ayaka")
DEFAULT_VERSION = os.environ.get("RVC_DEFAULT_VERSION", "v1")

app = FastAPI()
rvc: Optional[RVCInference] = None
current_model_name: Optional[str] = None
current_version: str = DEFAULT_VERSION


@app.on_event("startup")
def startup() -> None:
    global rvc, current_model_name
    rvc = RVCInference(models_dir=MODELS_DIR, device=DEVICE)
    if DEFAULT_MODEL in rvc.list_models():
        rvc.load_model(DEFAULT_MODEL, version=DEFAULT_VERSION)
        current_model_name = DEFAULT_MODEL


@app.get("/health")
def health() -> dict:
    return {
        "status": "ok",
        "device": DEVICE,
        "model": current_model_name,
        "models": rvc.list_models() if rvc else [],
    }


@app.get("/models")
def models() -> dict:
    return {"models": rvc.list_models() if rvc else [], "current": current_model_name}


@app.post("/load_model")
async def load_model(request: Request) -> dict:
    global current_model_name, current_version
    body = await request.json()
    name = body["name"]
    version = body.get("version", "v1")
    rvc.load_model(name, version=version)
    current_model_name = name
    current_version = version
    return {"ok": True, "model": name}


@app.post("/convert")
async def convert(request: Request):
    if not current_model_name:
        return JSONResponse({"error": "no model loaded"}, status_code=400)

    params = {
        "f0up_key": int(request.query_params.get("pitch", 12)),
        "index_rate": float(request.query_params.get("index_rate", 0.3)),
        "protect": float(request.query_params.get("protect", 0.5)),
        "f0method": request.query_params.get("f0method", "rmvpe"),
        "filter_radius": int(request.query_params.get("filter_radius", 3)),
    }
    rvc.set_params(**params)

    audio_bytes = await request.body()
    if not audio_bytes:
        return JSONResponse({"error": "empty body"}, status_code=400)

    with tempfile.TemporaryDirectory() as tmp_dir:
        in_path = os.path.join(tmp_dir, "in.mp3")
        out_path = os.path.join(tmp_dir, "out.wav")
        with open(in_path, "wb") as f:
            f.write(audio_bytes)

        rvc.infer_file(in_path, out_path)

        with open(out_path, "rb") as f:
            wav_bytes = f.read()

    return Response(content=wav_bytes, media_type="audio/wav")
