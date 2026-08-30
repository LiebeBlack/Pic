import os
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

def generate():
    root = Path(__file__).resolve().parent.parent
    res_dir = root / "resources"
    res_dir.mkdir(exist_ok=True)
    ico_path = res_dir / "artpicst.ico"

    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = []
    for s in sizes:
        im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        pad = max(1, int(s * 0.12))
        d.rounded_rectangle((pad, pad, s - pad, s - pad), radius=max(4, int(s * 0.16)), fill=(15, 23, 34, 255))
        d.rounded_rectangle((max(1, int(s * 0.22)), max(1, int(s * 0.22)), s - max(1, int(s * 0.22)), s - max(1, int(s * 0.22))), radius=max(4, int(s * 0.12)), fill=(27, 34, 42, 255))
        try:
            font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", max(10, int(s * 0.60)))
        except Exception:
            font = ImageFont.load_default()
        bbox = d.textbbox((0, 0), "A", font=font)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        x = (s - tw) / 2
        y = (s - th) / 2 - int(s * 0.04)
        d.text((x, y), "A", font=font, fill=(102, 176, 255, 255))
        d.rounded_rectangle((int(s * 0.30), int(s * 0.34), int(s * 0.68), int(s * 0.72)), radius=max(2, int(s * 0.06)), fill=(255, 255, 255, 28))
        images.append(im)

    images[0].save(ico_path, format="ICO", sizes=[(s, s) for s in sizes])
    print(f"Icono generado: {ico_path}")

if __name__ == "__main__":
    generate()
