"""Recalculate Table I scenario means from the submitted report."""
import csv
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
for scenario, values in scenarios.items():
    report_mean = mean(printed for printed, _ in values)
    raw_mean = mean(raw for _, raw in values)
    if len(values) != 3 or round(report_mean, 2) != expected[scenario]:
        raise ValueError(f"Unexpected trial summary for {scenario}")
    print(f"{scenario}: {report_mean:.2f}% from printed errors, {raw_mean:.2f}% from raw counts")
