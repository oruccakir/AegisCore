"""AegisCore person/no-person inference server."""

import base64
import io
import os
from typing import Any

import torch
import torchvision.models as models
from torchvision.models.detection import SSDLite320_MobileNet_V3_Large_Weights
from PIL import Image
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

app = FastAPI(title="AegisCore Inference", version="1.0.0")

PERSON_LABEL_ID = 1
PERSON_THRESHOLD = float(os.environ.get("PERSON_THRESHOLD", "0.50"))

_model_error: str | None = None
_weights: SSDLite320_MobileNet_V3_Large_Weights | None = None
_model: torch.nn.Module | None = None
_transform: Any | None = None

try:
    _weights = SSDLite320_MobileNet_V3_Large_Weights.DEFAULT
    _model = models.detection.ssdlite320_mobilenet_v3_large(weights=_weights)
    _model.eval()
    _transform = _weights.transforms()
except Exception as exc:  # pragma: no cover - depends on local model availability
    _model_error = str(exc)


class InferRequest(BaseModel):
    jpeg_b64: str


class InferResponse(BaseModel):
    class_id: int
    class_name: str
    confidence: float


@app.post("/infer", response_model=InferResponse)
async def infer(req: InferRequest) -> InferResponse:
    if _model is None or _transform is None:
        raise HTTPException(status_code=503, detail=f"model unavailable: {_model_error}")

    try:
        img_bytes = base64.b64decode(req.jpeg_b64)
        img = Image.open(io.BytesIO(img_bytes)).convert("RGB")
    except Exception as exc:
        raise HTTPException(status_code=400, detail=f"Invalid image data: {exc}") from exc

    tensor = _transform(img).unsqueeze(0)
    with torch.no_grad():
        output = _model(tensor)[0]

    labels = output["labels"]
    scores = output["scores"]
    person_scores = scores[labels == PERSON_LABEL_ID]
    best_person_conf = float(person_scores.max().item()) if person_scores.numel() > 0 else 0.0

    if best_person_conf >= PERSON_THRESHOLD:
        return InferResponse(class_id=1, class_name="person", confidence=best_person_conf)

    return InferResponse(
        class_id=0,
        class_name="none",
        confidence=max(0.0, min(1.0, 1.0 - best_person_conf)),
    )


@app.get("/health")
async def health() -> dict:
    return {
        "status": "ok" if _model is not None else "degraded",
        "model": "ssdlite320_mobilenet_v3_large",
        "mode": "person/no-person",
        "threshold": PERSON_THRESHOLD,
        "error": _model_error,
    }


if __name__ == "__main__":
    import uvicorn
    port = int(os.environ.get("PORT", "7979"))
    uvicorn.run(app, host="0.0.0.0", port=port)
