#!/usr/bin/env python3
import argparse, os, sys, csv, shutil
from typing import Optional
import olefile


WORD_STREAM = "WordDocument"
XLS_STREAMS = {"Workbook", "Book"}
PPT_STREAM  = "PowerPoint Document"

MISMATCH_DIRNAME = {
    "doc": "(mismatch)doc",
    "xls": "(mismatch)xls",
    "ppt": "(mismatch)ppt",
    "hwp": "(mismatch)hwp",
}

def expected_from_ext(path: str) -> Optional[str]:
    e = os.path.splitext(path)[1].lower()
    return {".doc":"doc", ".xls":"xls", ".ppt":"ppt", ".hwp":"hwp"}.get(e)

def is_hwp(ole: olefile.OleFileIO) -> bool:
    if not ole.exists("FileHeader"):
        return False
    try:
        data = ole.openstream("FileHeader").read(64)
    except Exception:
        return False
    sig = data[:32].rstrip(b"\x00")
    return sig == b"HWP Document File"


def detect_ole_type(path: str) -> str:
    """doc/xls/ppt/unknown/not_ole/corrupt"""
    try:
        if not olefile.isOleFile(path):
            return "not_ole"
        with olefile.OleFileIO(path) as ole:
            if is_hwp(ole):
                return "hwp"
            names = {".".join(p) for p in ole.listdir()}
        if any(WORD_STREAM in n for n in names): return "doc"
        if any(n in XLS_STREAMS for n in names): return "xls"
        if any(PPT_STREAM in n for n in names):  return "ppt"
        return "unknown"
    except Exception:
        return "corrupt"
    

def move_to(dst_dir: str, path: str):
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.basename(path)
    dst  = os.path.join(dst_dir, base)
    i = 1
    while os.path.exists(dst):
        root, ext = os.path.splitext(dst)
        dst = f"{root}__{i}{ext}"
        i += 1
    shutil.move(path, dst)
    return dst

def output_root_of(file_path: str) -> str:
    # .../outputDocument/doc-11-0/00000001.doc  ->  .../outputDocument
    return os.path.dirname(os.path.dirname(file_path))

def collect_files(roots):
    paths = []
    for r in roots:
        for dp, _, files in os.walk(r):
            for f in files:
                paths.append(os.path.join(dp, f))
    return paths

def main():
    ap = argparse.ArgumentParser(
        description="Verify carved DOC/XLS/PPT (OLE) by substreams; delete zero-byte; move mismatches under output root.")
    ap.add_argument("mode", choices=["save", "none"],
                help="save: keep carved files, none: classify only and do not preserve outputs")
    ap.add_argument("roots", nargs="+",
                    help="output root dirs (e.g., outputDocument or out_doc/out_xls/out_ppt)")
    ap.add_argument("--csv", default="ole_verify_report.csv",
                    help="report CSV filename")
    ap.add_argument("--fix", action="store_true",
                    help="if set, actually move mismatches into (mismatch) folders")
    args = ap.parse_args()

    rows = []
    counts = {"checked":0, "deleted_zero":0, "ok":0, "mismatch":0,
              "unknown":0, "corrupt":0, "not_ole":0}

    all_paths = collect_files(args.roots)

    for path in all_paths:
        if not os.path.exists(path):
            rows.append([path, "(missing)", "", "skipped_missing", "0"])
            continue

        try:
            size = os.path.getsize(path)
        except FileNotFoundError:
            continue

        if size == 0:
            try:
                os.remove(path)
                counts["deleted_zero"] += 1
                rows.append([path, "(deleted zero)", "", "", "0"])
            except FileNotFoundError:
                pass
            continue

        exp = expected_from_ext(path)
        if exp is None:
            continue

        det = detect_ole_type(path)
        counts["checked"] += 1

        if det == exp and det in {"doc","xls","ppt","hwp"}:
            counts["ok"] += 1
            rows.append([path, exp, det, "ok", str(size)])
            continue

        # mismatch
        mm_dirname = MISMATCH_DIRNAME.get(exp, "(mismatch)unknown")
        root = output_root_of(path)
        dst_dir = os.path.join(root, mm_dirname)

        try:
            if args.mode == "save":
                if args.fix:
                    newp = move_to(dst_dir, path)
                    rows.append([path, exp, det, f"mismatch -> {newp}", str(size)])
                else:
                    rows.append([path, exp, det, f"mismatch (would move to {dst_dir})", str(size)])
            else:
                # none mode: keep no carved artifact
                try:
                    os.remove(path)
                    rows.append([path, exp, det, "mismatch -> deleted (none mode)", str(size)])
                except FileNotFoundError:
                    rows.append([path, exp, det, "mismatch -> already missing (none mode)", str(size)])

            counts["mismatch"] += 1
        except Exception:
            rows.append([path, exp, det, "move_failed", str(size)])
            if det in counts:
                counts[det] += 1

    with open(args.csv, "w", newline="", encoding="utf-8") as fp:
        w = csv.writer(fp)
        w.writerow(["path","expected_ext","detected_type","action","size_bytes"])
        w.writerows(rows)

    print("Summary:", counts)
    print(f"Report saved to {args.csv}")
    if not args.fix:
        print("Note: --fix not set, mismatches were only reported (not moved).")


if __name__ == "__main__":
    sys.exit(main())
