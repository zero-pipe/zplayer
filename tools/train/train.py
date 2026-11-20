#!/usr/bin/env python3
"""Local fine-tuning entry used by the Zeroo player's "训练" page.

The app exports a YOLO dataset (images/ labels/ data.yaml classes.txt) and then
runs this script with:

    python tools/train/train.py --data <exportDir> --epochs 200 --imgsz 960 \
        --out <exportDir>/best.onnx --metrics <exportDir>/metrics.json

It performs transfer learning from a pretrained yolo11n, exports ONNX (opset 17,
which the app's DirectML backend loads), and writes metrics.json so the app's
评估 page can gate publishing.

Requires: pip install ultralytics onnx onnxruntime
"""
import argparse
import json
import os
import shutil


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="export dir (has data.yaml) or a data.yaml")
    ap.add_argument("--base", default="yolo11n.pt", help="pretrained weights")
    ap.add_argument("--epochs", type=int, default=200)
    ap.add_argument("--imgsz", type=int, default=960)
    ap.add_argument("--batch", type=int, default=-1)
    ap.add_argument("--device", default="0")
    ap.add_argument("--out", required=True, help="output .onnx path")
    ap.add_argument("--metrics", required=True, help="output metrics.json path")
    args = ap.parse_args()

    data = args.data
    if os.path.isdir(data):
        data = os.path.join(data, "data.yaml")
    if not os.path.isfile(data):
        print(f"[train] data.yaml not found: {data}")
        return 2

    from ultralytics import YOLO

    print(f"[train] base={args.base} data={data} epochs={args.epochs} imgsz={args.imgsz}")
    model = YOLO(args.base)

    # Two-stage: freeze the backbone first, then a short unfreeze refine pass.
    freeze_epochs = max(1, args.epochs // 3)
    model.train(
        data=data,
        epochs=freeze_epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        device=args.device,
        optimizer="AdamW",
        lr0=1e-3,
        cos_lr=True,
        freeze=10,
        mosaic=1.0,
        mixup=0.1,
        copy_paste=0.1,
        hsv_h=0.015,
        hsv_s=0.7,
        hsv_v=0.4,
        patience=40,
        project="runs",
        name="zerobase",
        exist_ok=True,
    )
    if args.epochs - freeze_epochs > 0:
        model.train(
            data=data,
            epochs=args.epochs - freeze_epochs,
            imgsz=args.imgsz,
            batch=args.batch,
            device=args.device,
            optimizer="AdamW",
            lr0=3e-4,
            cos_lr=True,
            mosaic=1.0,
            mixup=0.1,
            hsv_h=0.015,
            hsv_s=0.7,
            hsv_v=0.4,
            patience=40,
            project="runs",
            name="zerofine",
            exist_ok=True,
        )

    # Validate and persist metrics for the app's gate.
    metrics = model.val(data=data, imgsz=args.imgsz, device=args.device)
    try:
        md = {k: float(v) for k, v in metrics.results_dict.items()}
    except Exception:
        md = {}
    os.makedirs(os.path.dirname(os.path.abspath(args.metrics)), exist_ok=True)
    with open(args.metrics, "w", encoding="utf-8") as f:
        json.dump(md, f, indent=2, ensure_ascii=False)
    print(f"[train] metrics -> {args.metrics}")

    # Export ONNX (opset 17 works with the app's DirectML runtime).
    exported = model.export(format="onnx", opset=17, imgsz=args.imgsz)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    shutil.copyfile(exported, args.out)
    print(f"[train] onnx -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
