#!/usr/bin/env python3
import os
import csv
import argparse
import shutil

CATEGORY_DIR = {
    "partial": "(partial)",
    "invalid": "(invalid)",
    "not_pdf": "(not_pdf)",
}

def expected_from_ext(path: str):
    ext = os.path.splitext(path)[1].lower()
    return {".pdf": "pdf"}.get(ext)

def move_to(dst_dir: str, path: str) -> str:
    os.makedirs(dst_dir, exist_ok=True)
    base = os.path.basename(path)
    dst = os.path.join(dst_dir, base)
    i = 1
    while os.path.exists(dst):
        root, ext = os.path.splitext(dst)
        dst = f"{root}__{i}{ext}"
        i += 1
    shutil.move(path, dst)
    return dst

def delete_file(path: str) -> str:
    try:
        os.remove(path)
        return "deleted"
    except FileNotFoundError:
        return "already_missing"
    except Exception as e:
        return f"delete_fail:{type(e).__name__}"

def output_root_of(file_path: str) -> str:
    # .../outputDocument/pdf-0/00000001.pdf -> .../outputDocument
    return os.path.dirname(os.path.dirname(file_path))

def collect_files(roots):
    for r in roots:
        for dp, _, files in os.walk(r):
            for f in files:
                yield os.path.join(dp, f)

def validate_pdf(path: str) -> str:
    """
    return: 'ok' / 'partial' / 'invalid' / 'not_pdf'
    """
    try:
        with open(path, "rb") as fp:
            header = fp.read(5)
            if len(header) < 5:
                return "invalid"

            if header != b"%PDF-":
                return "not_pdf"

            fp.seek(0, os.SEEK_END)
            filesize = fp.tell()
            if filesize < 20:
                return "invalid"

            BUF_SIZE = 1024
            start_pos = filesize - BUF_SIZE if filesize > BUF_SIZE else 0
            fp.seek(start_pos, os.SEEK_SET)
            tail = fp.read(BUF_SIZE)

    except Exception:
        return "invalid"

    if b"%%EOF" in tail:
        return "ok"
    else:
        return "partial"


def main():
    ap = argparse.ArgumentParser(
        description="Verify carved PDF files; delete zero-byte; move partial/invalid/not_pdf."
    )
    ap.add_argument("mode", choices=["save", "none"],
                help="save: move bad outputs into folders, none: delete bad outputs")
    ap.add_argument("roots", nargs="+",
                    help="output root dirs (e.g., outputDocument)")
    ap.add_argument("--csv", default="pdf_verify_report.csv",
                    help="report CSV filename")
    ap.add_argument("--fix", action="store_true",
                    help="actually move partial/invalid/not_pdf into folders")
    args = ap.parse_args()

    rows = []
    counts = {
        "checked": 0,
        "deleted_zero": 0,
        "ok": 0,
        "partial": 0,
        "invalid": 0,
        "not_pdf": 0,
        "skip_non_pdf_ext": 0,
    }

    for path in collect_files(args.roots):
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
            counts["skip_non_pdf_ext"] += 1
            rows.append([path, "(skip_non_pdf_ext)", "none", "skip", str(size)])
            continue

        counts["checked"] += 1

        v = validate_pdf(path)

        if v == "ok":
            cat = "ok"
            action = "ok"
        elif v == "partial":
            cat = "partial"
            action = "partial_structure"
        elif v == "not_pdf":
            cat = "not_pdf"
            action = "not_pdf_header"
        else:
            cat = "invalid"
            action = "invalid_structure"

        counts[cat] += 1

        if cat in ("partial", "invalid", "not_pdf"):
            root = output_root_of(path)
            dst_dir = os.path.join(root, CATEGORY_DIR[cat])

            if args.fix:
                if args.mode == "save":
                    newp = move_to(dst_dir, path)
                    action = f"{action} -> {newp}"
                else:
                    result = delete_file(path)
                    action = f"{action} -> {result}"
            else:
                if args.mode == "save":
                    action = f"{action} (would move to {dst_dir})"
                else:
                    action = f"{action} (would delete)"

        rows.append([path, exp, v, action, str(size)])

    with open(args.csv, "w", newline="", encoding="utf-8") as fp:
        w = csv.writer(fp)
        w.writerow(["path", "expected_type", "validate_result",
                    "action", "size_bytes"])
        w.writerows(rows)

    print("Summary:", counts)
    print(f"Report saved to {args.csv}")
    if not args.fix:
        print("Note: --fix not set, files were not moved (only reported).")


if __name__ == "__main__":
    main()
