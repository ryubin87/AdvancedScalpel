#!/usr/bin/env python3
import os
import csv
import argparse
import shutil
import subprocess
import struct

CATEGORY_DIR = {
    "partial": "(partial)",
    "invalid": "(invalid)",
    "not_media": "(not_media)",
}

VALID_EXT = {".mov", ".mp4", ".avi", ".wav"}

def expected_from_ext(path: str):
    ext = os.path.splitext(path)[1].lower()
    return {
        ".mov": "movmp4",
        ".mp4": "movmp4",
        ".avi": "avi",
        ".wav": "wav",
    }.get(ext)

def ffprobe_has_stream(path: str) -> bool:
    """
    ffprobe로 stream 존재 여부만 체크.
    duration 대신 stream 정보가 있으면 OK로 본다.
    """
    try:
        cmd = [
            "ffprobe", "-v", "error",
            "-show_entries", "stream=index",
            "-of", "csv=p=0",
            path
        ]
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT).decode().strip()
        return len(out) > 0
    except:
        return False



def probe_media_quick(path: str) -> bool:
    """
    ffprobe 기반 최종 정상 여부 판별.
    """
    return ffprobe_has_stream(path)


# ---------------------------------------------------------
# 원래 헤더 스니핑은 타입을 추론하는용도로만
# ---------------------------------------------------------
def sniff_media_type(path: str) -> str:
    ext = os.path.splitext(path)[1].lower()
    if ext in (".mp4", ".mov"):
        return "movmp4"
    if ext == ".avi":
        return "avi"
    if ext == ".wav":
        return "wav"
    return "unknown"


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


def output_root_of(file_path: str) -> str:
    # .../outputMedia/mp4-0/00000001.mp4 -> .../outputMedia
    return os.path.dirname(os.path.dirname(file_path))


def collect_files(roots):
    for r in roots:
        for dp, _, files in os.walk(r):
            for f in files:
                yield os.path.join(dp, f)


# ---------------------------------------------------------
# 구조 검증은 보조 수단 → ffprobe 실패시에만 사용
# ---------------------------------------------------------

def validate_movmp4_struct(path: str) -> str:
    try:
        f = open(path, "rb")
    except:
        return "invalid"

    with f:
        f.seek(0, os.SEEK_END)
        fsz = f.tell()
        if fsz < 16:
            return "invalid"

        off = 0
        seen_ftyp = False
        seen_moov = False
        seen_moof = False
        mdat_big = False
        partial = False

        while off + 8 <= fsz:
            f.seek(off)
            h = f.read(8)
            if len(h) < 8:
                partial = True
                break

            sz = struct.unpack(">I", h[:4])[0]
            typ = h[4:8]
            hdr = 8

            if sz == 1:
                ext = f.read(8)
                if len(ext) < 8:
                    partial = True
                    break
                sz64 = struct.unpack(">Q", ext)[0]
                hdr = 16
                box_end = off + sz64
            elif sz == 0:
                box_end = fsz
            else:
                box_end = off + sz

            if box_end > fsz or box_end <= off:
                partial = True
                break

            payload = off + hdr

            if typ == b"ftyp":
                seen_ftyp = True
            elif typ == b"moov":
                seen_moov = True
            elif typ == b"moof":
                seen_moof = True
            elif typ == b"mdat":
                size = box_end - payload
                if size >= 2048:  # 완화 (기존 4096)
                    mdat_big = True

            off = box_end

        if partial:
            return "partial"
        if not seen_ftyp:
            return "partial"    # 완화
        if not (seen_moov or seen_moof):
            return "partial"    # 완화
        if not mdat_big:
            return "partial"    # 완화
        return "ok"


def validate_avi_struct(path: str) -> str:
    try:
        f = open(path, "rb")
    except:
        return "invalid"

    with f:
        h = f.read(12)
        if len(h) < 12 or h[:4] != b"RIFF" or h[8:12] != b"AVI ":
            return "partial"    # 완화

        return "ok"  # AVI는 너무 다양하므로 강하게 완화


def validate_wav_struct(path: str) -> str:
    try:
        f = open(path, "rb")
    except:
        return "invalid"

    with f:
        h = f.read(12)
        if len(h) < 12 or h[:4] != b"RIFF" or h[8:12] != b"WAVE":
            return "partial"

        return "ok"


# ---------------------------------------------------------
# main
# ---------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--csv", default="media_verify_report.csv")
    ap.add_argument("--fix", action="store_true")
    args = ap.parse_args()

    rows = []
    counts = {
        "checked": 0,
        "ok": 0,
        "partial": 0,
        "invalid": 0,
        "not_media": 0,
    }

    for path in collect_files(args.roots):
        if not os.path.isfile(path):
            continue

        size = os.path.getsize(path)
        if size == 0:
            os.remove(path)
            continue

        ext = os.path.splitext(path)[1].lower()
        if ext not in VALID_EXT:
            # 완전 무시
            rows.append([path, "skip", "skip", "skip", str(size)])
            continue

        counts["checked"] += 1

        # 1) ffprobe로 최종 판단
        if probe_media_quick(path):
            cat = "ok"
            counts["ok"] += 1
            rows.append([path, ext, "ffprobe_ok", "ok", str(size)])
            continue

        # 2) ffprobe 실패시: 구조검증 보조
        mtype = sniff_media_type(path)
        if mtype == "movmp4":
            v = validate_movmp4_struct(path)
        elif mtype == "avi":
            v = validate_avi_struct(path)
        elif mtype == "wav":
            v = validate_wav_struct(path)
        else:
            v = "invalid"

        cat = v
        counts[cat] += 1

        # 이동
        if cat in ("partial", "invalid", "not_media") and args.fix:
            root = output_root_of(path)
            dst = os.path.join(root, CATEGORY_DIR.get(cat, "(invalid)"))
            newp = move_to(dst, path)
            action = f"{cat} -> {newp}"
        else:
            action = cat

        rows.append([path, ext, mtype, action, str(size)])

    with open(args.csv, "w", newline="", encoding="utf-8") as fp:
        wr = csv.writer(fp)
        wr.writerow(["path", "ext", "detected", "action", "size"])
        wr.writerows(rows)

    print("Summary:", counts)


if __name__ == "__main__":
    main()
