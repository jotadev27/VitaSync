#!/usr/bin/env python3
"""Build the offline title index that ships with VitaSync.

Reads the public NoPayStation catalogue exports and keeps only the three
columns needed to identify a package the user already owns: Title ID, region
and display name. Download links, content IDs and licence keys are dropped on
purpose -- the app has no use for them and they have no business in a local
metadata index.
"""
import csv, json, os, sys, datetime

SCRATCH = os.environ.get("VSP_TSV_DIR", os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["PSV_GAMES.tsv", "PSV_DEMOS.tsv", "PSV_UPDATES.tsv"]

REGION_NAMES = {"US": "US", "EU": "EU", "JP": "JP", "ASIA": "ASIA", "INT": "INT"}

titles = {}
counts = {}

for source in SOURCES:
    path = os.path.join(SCRATCH, source)
    if not os.path.exists(path):
        continue
    kept = 0
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            title_id = (row.get("Title ID") or "").strip().upper()
            name = (row.get("Name") or "").strip()
            region = REGION_NAMES.get((row.get("Region") or "").strip().upper(), "")
            if len(title_id) != 9 or not name or name.upper() == "MISSING":
                continue
            # Games win over updates/demos for the display name.
            existing = titles.get(title_id)
            if existing and source != "PSV_GAMES.tsv":
                continue
            titles[title_id] = {"name": name, "region": region} if region else {"name": name}
            kept += 1
    counts[source] = kept

payload = {
    "schema": 1,
    "generated": datetime.date.today().isoformat(),
    "source": "NoPayStation public catalogue exports (identification columns only)",
    "titleCount": len(titles),
    "titles": dict(sorted(titles.items())),
}

out = sys.argv[1]
with open(out, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, ensure_ascii=False, indent=0, separators=(",", ":"))
    handle.write("\n")

print(f"{len(titles)} titles -> {out}")
for source, kept in counts.items():
    print(f"  {source}: {kept}")
