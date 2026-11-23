"""Recalculate Table I scenario means from the submitted report."""
import csv
from html import escape
from pathlib import Path
from statistics import mean

trials = Path(__file__).resolve().parents[1] / "ist-submission" / "trials.csv"
scenarios = {}
with trials.open(encoding="utf-8", newline="") as stream:
    for row in csv.DictReader(stream):
        manual = int(row["manual_steps"])
        detected = int(row["detected_steps"])
        raw_error = 100 * abs(detected - manual) / manual
        printed_error = float(row["reported_error_pct"])
        if abs(raw_error - printed_error) > 0.0500001:
            raise ValueError(f"Reported error exceeds rounding tolerance: {row}")
        scenarios.setdefault(row["scenario"], []).append((printed_error, raw_error))

expected = {"Straight walk": 1.27, "Slow walk": 4.67, "Irregular pace": 7.30}
if scenarios.keys() != expected.keys():
    raise ValueError("Unexpected trial scenarios")
chart = [
    '<svg xmlns="http://www.w3.org/2000/svg" width="900" height="325" viewBox="0 0 900 325" font-family="Arial,sans-serif" role="img">',
    '<title>Mean absolute step-count error by walking condition</title>',
    '<rect width="900" height="325" fill="white"/>',
    '<text x="32" y="46" fill="#16265a" font-size="26" font-weight="700">Step counts were closest during straight walking</text>',
    '<text x="32" y="76" fill="#526579" font-size="17">Mean absolute percentage error · three trials per condition</text>',
]
for scenario, values in scenarios.items():
    report_mean = mean(printed for printed, _ in values)
    raw_mean = mean(raw for _, raw in values)
    if len(values) != 3 or round(report_mean, 2) != expected[scenario]:
        raise ValueError(f"Unexpected trial summary for {scenario}")
    print(f"{scenario}: {report_mean:.2f}% from printed errors, {raw_mean:.2f}% from raw counts")
    y = 118 + 56 * list(expected).index(scenario)
    width = 62 * raw_mean
    color = "#935784" if scenario == "Irregular pace" else "#16265a"
    chart.extend([
        f'<text x="32" y="{y+19}" fill="#293244" font-size="19">{escape(scenario)}</text>',
        f'<rect x="218" y="{y}" width="496" height="24" rx="4" fill="#eef1f6"/>',
        f'<rect x="218" y="{y}" width="{width:.2f}" height="24" rx="4" fill="{color}"/>',
        f'<text x="731" y="{y+19}" fill="{color}" font-size="19" font-weight="700">{raw_mean:.2f}%</text>',
    ])
chart.append('<text x="32" y="300" fill="#526579" font-size="16">Recalculated from the manual and detected counts in Table I of the IST report.</text></svg>')
destination = trials.parent / "figures" / "trial-errors.svg"
with destination.open("w", encoding="utf-8", newline="\n") as stream:
    stream.write("\n".join(chart) + "\n")
