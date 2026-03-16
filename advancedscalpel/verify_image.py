#!/usr/bin/env python3
import argparse, os, sys, csv, shutil, struct, zlib
from typing import Optional

CATEGORY_DIR = {
    "partial":   "(partial)",
    "invalid":   "(invalid)",
    "not_image": "(not_image)",
}

PNG_HEADER = b"\x89PNG\r\n\x1a\n"

def expected_from_ext(path: str) -> Optional[str]:
    e = os.path.splitext(path)[1].lower()
    return {
        ".png":  "png",
        ".jpg":  "jpeg",
        ".jpeg": "jpeg",
        ".gif":  "gif",
    }.get(e)

def sniff_image_type(path: str) -> str:
    try:
        with open(path, "rb") as f:
            head = f.read(10)
    except Exception:
        return "corrupt"

    if len(head) < 2:
        return "unknown"

    # PNG
    if head.startswith(PNG_HEADER):
        return "png"

    # JPEG
    if head[0:2] == b"\xFF\xD8":
        return "jpeg"

    # GIF
    if head.startswith(b"GIF87a") or head.startswith(b"GIF89a"):
        return "gif"

    return "unknown"

def validate_png_bytes(buf: bytes) -> str:
    """
    return: 'ok' / 'partial' / 'invalid'
    """
    if len(buf) < 8 or buf[:8] != PNG_HEADER:
        return "invalid"

    off = 8 
    last_good = off
    seen_IHDR = False
    seen_IEND = False
    max_chunk = 1 << 28  # 256MB

    while off + 12 <= len(buf):
        clen = struct.unpack(">I", buf[off:off+4])[0]
        ctype = buf[off+4:off+8]
        data_off = off + 8
        data_end = data_off + clen
        crc_off = data_end
        next_off = crc_off + 4

        if clen > max_chunk or next_off > len(buf):
            return "partial" if last_good > 8 else "invalid"

        crc_calc = zlib.crc32(ctype)
        crc_calc = zlib.crc32(buf[data_off:data_end], crc_calc) & 0xFFFFFFFF
        crc_read = struct.unpack(">I", buf[crc_off:crc_off+4])[0]

        if ctype == b"IHDR":
            if clen != 13 or crc_calc != crc_read:
                return "invalid"
            seen_IHDR = True

        elif ctype == b"IDAT":
            if crc_calc != crc_read:
                return "partial" if last_good > 8 else "invalid"

        elif ctype == b"IEND":
            if crc_calc != crc_read:
                return "partial" if last_good > 8 else "invalid"
            seen_IEND = True
            last_good = next_off
            return "ok" if (seen_IHDR and seen_IEND) else "invalid"

        else:
            if crc_calc != crc_read:
                return "partial" if last_good > 8 else "invalid"

        last_good = next_off
        off = next_off

    return "partial" if last_good > 8 else "invalid"


def validate_png_file(path: str) -> str:
    try:
        with open(path, "rb") as f:
            buf = f.read()
    except Exception:
        return "invalid"
    return validate_png_bytes(buf)

def validate_jpeg_file(path: str) -> str:
    try:
        with open(path, "rb") as fp:
            data = fp.read()
    except Exception:
        return "invalid"

    n = len(data)
    if n < 4:
        return "invalid"

    if not (data[0] == 0xFF and data[1] == 0xD8):
        return "invalid"

    i = 2
    seen_sof = False
    seen_sos = False
    seen_eoi = False

    while i < n:
        while i < n and data[i] != 0xFF:
            i += 1
        if i >= n:
            break

        j = i + 1
        while j < n and data[j] == 0xFF:
            j += 1
        if j >= n:
            break

        marker = data[j]

        if marker == 0xD9:        # EOI
            seen_eoi = True
            break
        if marker in (0xD8, 0x01) or (0xD0 <= marker <= 0xD7):
            i = j + 1
            continue

        if j + 2 >= n:
            return "invalid"

        seg_len = (data[j+1] << 8) | data[j+2]
        if seg_len < 2:
            return "invalid"

        seg_end = j + 1 + seg_len
        if seg_end > n:
            return "invalid"

        if marker in (0xC0, 0xC1, 0xC2, 0xC3,
                      0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB,
                      0xCD, 0xCE, 0xCF):
            seen_sof = True

        if marker == 0xDA:
            seen_sos = True
            k = seg_end
            while k + 1 < n:
                if data[k] == 0xFF and data[k+1] == 0xD9:
                    seen_eoi = True
                    break
                k += 1
            break

        i = seg_end

    if not seen_sof:
        return "invalid"
    if not seen_sos or not seen_eoi:
        return "invalid"

    return "ok"

def validate_gif_file(path: str) -> str:
    try:
        with open(path, "rb") as fp:
            header = fp.read(13)
            rest = fp.read()
    except Exception:
        return "invalid"

    if len(header) < 13:
        return "invalid"

    sig = header[:6]
    if sig not in (b"GIF87a", b"GIF89a"):
        return "invalid"

    width  = header[6] | (header[7] << 8)
    height = header[8] | (header[9] << 8)
    if width == 0 or height == 0:
        return "invalid"

    if b"\x2C" not in rest:
        return "invalid"

    return "ok"

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

def delete_file(path: str) -> str:
    try:
        os.remove(path)
        return "deleted"
    except FileNotFoundError:
        return "already_missing"
    except Exception as e:
        return f"delete_fail:{type(e).__name__}"

def output_root_of(file_path: str) -> str:
    # .../outputImage/png-11-0/00000001.png  ->  .../outputImage
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
        description="Verify carved PNG/JPEG/GIF structurally; delete zero-byte; move partial/invalid/not_image into folders."
    )
    ap.add_argument("mode", choices=["save", "none"],
                help="save: move bad outputs into folders, none: delete bad outputs")
    ap.add_argument("roots", nargs="+",
                    help="output root dirs (e.g., outputImage or out_png/out_jpeg/out_gif)")
    ap.add_argument("--csv", default="image_verify_report.csv",
                    help="report CSV filename")
    ap.add_argument("--fix", action="store_true",
                    help="if set, actually move partial/invalid/not_image into folders")
    args = ap.parse_args()

    rows = []
    counts = {
        "checked": 0,
        "deleted_zero": 0,
        "ok": 0,
        "partial": 0,
        "invalid": 0,
        "not_image": 0,
        "unknown": 0,
        "corrupt": 0,
    }

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
            rows.append([path, "(skip_non_image_ext)", "none", "skip", str(size)])
            continue

        header_type = sniff_image_type(path)
        counts["checked"] += 1

        if header_type in ("corrupt", "unknown"):
            counts[header_type] += 1
            cat = "not_image"
            counts[cat] += 1
            root = output_root_of(path)
            dst_dir = os.path.join(root, CATEGORY_DIR[cat])

            if args.fix:
                if args.mode == "save":
                    newp = move_to(dst_dir, path)
                    action = f"not_image_header_{header_type} -> {newp}"
                else:
                    result = delete_file(path)
                    action = f"not_image_header_{header_type} -> {result}"
            else:
                if args.mode == "save":
                    action = f"not_image_header_{header_type} (would move to {dst_dir})"
                else:
                    action = f"not_image_header_{header_type} (would delete)"

        if header_type == "png":
            v = validate_png_file(path)     # ok / partial / invalid
        elif header_type == "jpeg":
            v = validate_jpeg_file(path)    # ok / invalid
        elif header_type == "gif":
            v = validate_gif_file(path)     # ok / invalid
        else:
            v = "invalid"

        if header_type != exp:
            cat = "invalid"
            reason = f"invalid_ext_mismatch(type={header_type},structure={v})"
        else:
            if v == "ok":
                cat = "ok"
                reason = "ok"
            elif v == "partial":
                cat = "partial"
                reason = "partial_structure"
            else:
                cat = "invalid"
                reason = "invalid_structure"

        counts[cat] += 1

        if cat in ("partial", "invalid"):
            root = output_root_of(path)
            dst_dir = os.path.join(root, CATEGORY_DIR[cat])
            if args.fix:
                if args.mode == "save":
                    newp = move_to(dst_dir, path)
                    action = f"{reason} -> {newp}"
                else:
                    result = delete_file(path)
                    action = f"{reason} -> {result}"
            else:
                if args.mode == "save":
                    action = f"{reason} (would move to {dst_dir})"
                else:
                    action = f"{reason} (would delete)"
        else:
            action = reason

        rows.append([path, exp, header_type, action, str(size)])

    with open(args.csv, "w", newline="", encoding="utf-8") as fp:
        w = csv.writer(fp)
        w.writerow(["path", "expected_type", "detected_type", "action", "size_bytes"])
        w.writerows(rows)

    print("Summary:", counts)
    print(f"Report saved to {args.csv}")
    if not args.fix:
        print("Note: --fix not set, partial/invalid/not_image were only reported (not moved).")


if __name__ == "__main__":
    sys.exit(main())
