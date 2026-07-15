import os
import tempfile

import edge_tts
from fastapi import FastAPI
from fastapi.responses import JSONResponse, Response
from pydantic import BaseModel

app = FastAPI()


class SynthesizeRequest(BaseModel):
    text: str
    voice: str = "ru-RU-DmitryNeural"
    rate: str = "+0%"
    volume: str = "+0%"


@app.get("/health")
def health() -> dict:
    return {"status": "ok"}


@app.post("/synthesize")
async def synthesize(req: SynthesizeRequest):
    if not req.text.strip():
        return JSONResponse({"error": "empty text"}, status_code=400)

    tmp_path = None
    try:
        fd, tmp_path = tempfile.mkstemp(suffix=".mp3")
        os.close(fd)

        communicate = edge_tts.Communicate(req.text, req.voice, rate=req.rate, volume=req.volume)
        await communicate.save(tmp_path)

        with open(tmp_path, "rb") as f:
            audio_bytes = f.read()
    finally:
        if tmp_path and os.path.exists(tmp_path):
            os.unlink(tmp_path)

    return Response(content=audio_bytes, media_type="audio/mpeg")
