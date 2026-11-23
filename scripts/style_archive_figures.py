"""Frame the original report figures without changing their pixels."""
from base64 import b64encode
from html import escape
from pathlib import Path

FIGURES = Path(__file__).resolve().parents[1] / "ist-submission" / "figures"
PANELS = [
    ("Im4.jpg", "prototype", "The original pedometer prototype",
     "Arduino Nano 33 BLE and ultrasonic module on a breadboard", 650),
    ("Im5.jpg", "walking-trace", "Dynamic acceleration during walking",
     "The original report shows approximately 120 seconds of filtered acceleration", 630),
    ("Im6.png", "filtering-detail", "Filtering preserves the step-cycle peaks",
     "Raw acceleration magnitude and its filtered dynamic component", 630),
    ("Im7.png", "frequency-spectrum", "Frequency content of the walking signal",
     "FFT of raw acceleration from the original experiment", 630),
]

for number, (source, slug, heading, detail, height) in enumerate(PANELS, start=1):
    mime = "image/jpeg" if source.endswith(".jpg") else "image/png"
    encoded = b64encode((FIGURES / source).read_bytes()).decode("ascii")
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="900" height="{height}" viewBox="0 0 900 {height}" font-family="Arial, sans-serif" role="img" aria-labelledby="title">
  <title id="title">{escape(heading)}</title>
  <rect width="900" height="{height}" fill="white"/>
  <text x="32" y="36" font-size="16" font-weight="700" fill="#935784">IST SUBMISSION · FIGURE {number}</text>
  <text x="32" y="75" font-size="28" font-weight="700" fill="#16265a">{escape(heading)}</text>
  <text x="32" y="104" font-size="17" fill="#526579">{escape(detail)}</text>
  <rect x="32" y="126" width="836" height="{height-184}" rx="11" fill="#f4f5fa" stroke="#dee3ec"/>
  <image x="50" y="140" width="800" height="{height-212}" preserveAspectRatio="xMidYMid meet" xlink:href="data:{mime};base64,{encoded}"/>
  <path d="M32 {height-42}h836" stroke="#dee3ec"/>
  <text x="32" y="{height-17}" font-size="15" fill="#526579">Source: original IST report · figure reproduced as submitted</text>
</svg>
'''
    with (FIGURES / f"{slug}.svg").open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(svg)
