#!/usr/bin/env python3
"""Download missing Cinema videos for the maps on a connected Quest and copy them over.

For every map in SongCore's CustomLevels/CustomWIPLevels folders this reads the map's
cinema-video.json (or the bundled config PC Cinema ships for popular maps), downloads the
video with yt-dlp if the map folder doesn't have one yet, and pushes it with adb.

Only the video stream is downloaded: Cinema never plays a video's own audio.

Usage:
    python3 tools/cinema_sync.py              # download and push everything missing
    python3 tools/cinema_sync.py --dry-run    # only list what would be done
"""

import argparse
import json
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

QUEST_SONGCORE_DIR = "/sdcard/ModData/com.beatgames.beatsaber/Mods/SongCore"
LEVEL_FOLDERS = ["CustomLevels", "CustomWIPLevels"]
CONFIG_FILENAME = "cinema-video.json"
VIDEO_EXTENSIONS = (".mp4", ".webm", ".mkv", ".mov")
BUNDLED_CONFIGS = Path(__file__).resolve().parent.parent / "assets" / "configs.json"
CACHE_DIR = Path.home() / "Library" / "Caches" / "cinema-sync"


def adb(*args, check=True):
    return subprocess.run(["adb", *args], capture_output=True, check=check)


def adb_shell(command):
    return adb("shell", command).stdout.decode("utf-8", errors="replace")


def replace_illegal_filesystem_chars(s):
    # Same as Util.ReplaceIllegalFilesystemChars on PC and the Quest mod: Windows' invalid file name chars plus '.'
    return re.sub(r'[<>:"/\\|?*.\x00-\x1f]', "_", s)


def with_mp4(name):
    return name if name.endswith(".mp4") else name + ".mp4"


def video_file_name(config):
    """File name PC Cinema (and the Quest mod) look for first."""
    if config.get("videoFile"):
        return with_mp4(config["videoFile"])
    return with_mp4(replace_illegal_filesystem_chars(config.get("title") or config.get("videoID") or "video"))


def has_video(config, files):
    candidates = {video_file_name(config)}
    if config.get("title"):
        candidates.add(with_mp4(config["title"]))
    # The mod falls back to any video file in the folder
    return any(f in candidates or f.lower().endswith(VIDEO_EXTENSIONS) for f in files)


def list_maps():
    """Returns a list of (folder path, file names, parsed cinema-video.json or None)."""
    # One shell call for all maps, with markers between them
    script = (
        "for root in " + " ".join(shlex.quote(f"{QUEST_SONGCORE_DIR}/{folder}") for folder in LEVEL_FOLDERS) + "; do "
        '[ -d "$root" ] || continue; '
        'for d in "$root"/*/; do '
        'echo "@@DIR ${d%/}"; ls -1 "$d"; '
        f'if [ -f "${{d}}{CONFIG_FILENAME}" ]; then echo "@@JSON"; cat "${{d}}{CONFIG_FILENAME}"; echo; fi; '
        'echo "@@END"; done; done'
    )
    maps = []
    current = None
    for line in adb_shell(script).splitlines():
        if line.startswith("@@DIR "):
            current = {"dir": line[6:], "files": [], "json": None}
        elif line == "@@JSON":
            current["json"] = ""
        elif line == "@@END":
            config = None
            if current["json"]:
                try:
                    config = json.loads(current["json"].lstrip("﻿"))
                except json.JSONDecodeError:
                    print(f"  ! Could not parse {CONFIG_FILENAME} in {current['dir']}")
            maps.append((current["dir"], current["files"], config))
            current = None
        elif current is not None:
            if current["json"] is not None:
                current["json"] += line + "\n"
            else:
                current["files"].append(line)
    return maps


def load_level_hashes():
    raw = adb("exec-out", "cat", f"{QUEST_SONGCORE_DIR}/CachedSongData.json", check=False).stdout
    try:
        return {path: data["sha1"] for path, data in json.loads(raw).items() if data.get("sha1")}
    except (json.JSONDecodeError, AttributeError):
        print("  ! Could not read SongCore's CachedSongData.json, bundled configs won't be used")
        return {}


def load_bundled_configs():
    with open(BUNDLED_CONFIGS, encoding="utf-8") as f:
        return {entry["levelID"]: entry["config"] for entry in json.load(f)}


def download(config, max_height):
    """Downloads the video stream into the cache and returns its path."""
    video_id = config.get("videoID")
    url = config.get("videoUrl") or f"https://www.youtube.com/watch?v={video_id}"
    cache_name = replace_illegal_filesystem_chars(video_id or url)
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    existing = list(CACHE_DIR.glob(f"{cache_name}.*"))
    if existing:
        return existing[0]

    # Prefer H.264 since the Quest decodes it in hardware, fall back to any mp4 video
    video_format = (
        f"bv*[vcodec^=avc1][height<={max_height}][ext=mp4]/"
        f"bv*[height<={max_height}][ext=mp4]/"
        f"b[height<={max_height}][ext=mp4]/b"
    )
    result = subprocess.run(
        ["yt-dlp", "--no-playlist", "-f", video_format, "-o", str(CACHE_DIR / f"{cache_name}.%(ext)s"), url]
    )
    if result.returncode != 0:
        return None
    downloaded = [p for p in CACHE_DIR.glob(f"{cache_name}.*") if not p.name.endswith(".part")]
    return downloaded[0] if downloaded else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dry-run", action="store_true", help="only list what would be downloaded")
    parser.add_argument("--max-height", type=int, default=1080, help="maximum video height to download (default 1080)")
    args = parser.parse_args()

    for tool in ("adb", "yt-dlp"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is not installed")
    devices = [l for l in adb("devices").stdout.decode().splitlines()[1:] if l.strip().endswith("device")]
    if not devices:
        sys.exit("No Quest connected over adb")

    print("Reading maps from the Quest...")
    maps = list_maps()
    hashes = load_level_hashes()
    bundled = load_bundled_configs()

    todo = []
    with_config = 0
    for level_dir, files, config in maps:
        from_bundle = False
        if config is None:
            sha1 = hashes.get(level_dir)
            config = bundled.get(f"custom_level_{sha1}") if sha1 else None
            from_bundle = config is not None
        if config is None:
            continue
        with_config += 1
        if not has_video(config, files):
            todo.append((level_dir, config, from_bundle))

    print(f"{len(maps)} maps, {with_config} with a Cinema config, {len(todo)} missing their video\n")
    for level_dir, config, from_bundle in todo:
        print(f"- {Path(level_dir).name}: {config.get('title')}{' (bundled config)' if from_bundle else ''}")
    if args.dry_run or not todo:
        return

    failed = []
    for level_dir, config, from_bundle in todo:
        print(f"\n=== {Path(level_dir).name}")
        video = download(config, args.max_height)
        if video is None:
            print("  ! Download failed")
            failed.append(level_dir)
            continue

        # Name it the way Cinema looks for it, keeping the extension yt-dlp chose
        target_name = Path(video_file_name(config)).stem + video.suffix
        print(f"  Copying {video.name} -> {target_name}")
        if adb("push", str(video), f"{level_dir}/{target_name}", check=False).returncode != 0:
            failed.append(level_dir)
            continue

        if from_bundle:
            # Same as PC Cinema, which saves the bundled config next to the video once it's downloaded
            with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
                json.dump({**config, "bundledConfig": True}, f, indent=2)
            adb("push", f.name, f"{level_dir}/{CONFIG_FILENAME}", check=False)
            Path(f.name).unlink()

    print(f"\nDone: {len(todo) - len(failed)} added, {len(failed)} failed")
    for level_dir in failed:
        print(f"  ! {level_dir}")
    print("Restart Beat Saber or refresh songs if the new videos don't show up.")


if __name__ == "__main__":
    main()
