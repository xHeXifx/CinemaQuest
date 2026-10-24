#!/usr/bin/env python3
"""Work out a Cinema video's offset by matching the video's audio against the map's song.

Finds the map on the connected Quest, pulls its song file, gets the video's audio (from a local
video file, or as an audio-only download with yt-dlp) and cross-correlates the two. The result is
the "offset" for cinema-video.json: how far into the video it should be when the song starts.

Usage:
    python3 tools/cinema_offset.py "Blame Canada"            # print the offset
    python3 tools/cinema_offset.py "Blame Canada" --apply    # also save it to the map's config
    python3 tools/cinema_offset.py "Enemy" --video ~/Downloads/enemy.mp4

The first run creates a small Python environment with numpy in ~/Library/Caches/cinema-sync/venv.
"""

import os
import subprocess
import sys
from pathlib import Path

VENV_DIR = Path.home() / "Library" / "Caches" / "cinema-sync" / "venv"


def ensure_numpy():
    """Re-runs this script inside a venv with numpy if it isn't importable."""
    try:
        import numpy  # noqa: F401
        return
    except ImportError:
        pass
    venv_python = VENV_DIR / "bin" / "python3"
    # The venv's python is a symlink to the system one, so compare prefixes rather than executables
    if Path(sys.prefix).resolve() == VENV_DIR.resolve():
        sys.exit("numpy is missing from the venv, delete ~/Library/Caches/cinema-sync/venv and try again")
    if not venv_python.exists():
        print(f"Setting up numpy in {VENV_DIR} (first run only)...")
        subprocess.run([sys.executable, "-m", "venv", str(VENV_DIR)], check=True)
        subprocess.run([str(venv_python), "-m", "pip", "install", "--quiet", "numpy"], check=True)
    os.execv(str(venv_python), [str(venv_python), *sys.argv])


ensure_numpy()

import argparse  # noqa: E402
import json  # noqa: E402
import shutil  # noqa: E402
import tempfile  # noqa: E402

import numpy as np  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cinema_sync  # noqa: E402

SAMPLE_RATE = 8000
# Onset envelope resolution: 5 ms, well below the 10 ms steps of the in-game offset buttons
HOP = 40
FRAME = 512
ENVELOPE_RATE = SAMPLE_RATE / HOP
SECTION_LENGTH = 30.0
SECTIONS = 4
# Sections whose offsets are this close agree with each other
AGREEMENT_MS = 60


def decode(path):
    """Decodes any audio or video file to mono float samples at SAMPLE_RATE."""
    result = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-vn", "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "f32le", "-"],
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError(f"ffmpeg couldn't decode {path}: {result.stderr.decode(errors='replace').strip()}")
    return np.frombuffer(result.stdout, dtype=np.float32)


def has_audio(path):
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "a", "-show_entries", "stream=index", "-of", "csv=p=0", str(path)],
        capture_output=True,
    )
    return bool(result.stdout.strip())


def onset_envelope(samples):
    """Spectral flux: how much the spectrum grows from one frame to the next, normalized.

    Matching changes in sound (beats, notes) rather than the waveform itself works even when the
    video's mix, mastering or loudness differs from the map's song.
    """
    count = 1 + max(0, (len(samples) - FRAME) // HOP)
    frames = np.lib.stride_tricks.as_strided(samples, shape=(count, FRAME), strides=(samples.strides[0] * HOP, samples.strides[0]))
    spectrum = np.log1p(np.abs(np.fft.rfft(frames * np.hanning(FRAME), axis=1)) * 10)
    flux = np.maximum(0, np.diff(spectrum, axis=0)).sum(axis=1)
    flux -= flux.mean()
    return flux / (flux.std() + 1e-9)


def best_lag(song, video):
    """Lag in envelope frames where video[t + lag] best matches song[t], plus how much the peak stands out."""
    size = 1 << int(np.ceil(np.log2(len(song) + len(video))))
    corr = np.fft.irfft(np.fft.rfft(video, size) * np.conj(np.fft.rfft(song, size)), size)
    # Index i is lag i for i < len(video), and lag i - size (negative) at the end
    lags = np.concatenate([np.arange(0, len(video)), np.arange(-len(song) + 1, 0)])
    values = np.concatenate([corr[: len(video)], corr[size - len(song) + 1 :]])
    peak = int(np.argmax(values))
    # Compare against the best match at least half a second away, to judge how unambiguous the peak is
    away = np.abs(lags - lags[peak]) > ENVELOPE_RATE / 2
    runner_up = values[away].max() if away.any() else 0
    return int(lags[peak]), float(values[peak] / (runner_up + 1e-9))


def find_offset(song_samples, video_samples):
    song = onset_envelope(song_samples)
    video = onset_envelope(video_samples)
    overall_lag, overall_score = best_lag(song, video)

    # Check sections of the song separately: if the video is a different edit (extra intro, cut verses) they disagree
    section_frames = int(SECTION_LENGTH * ENVELOPE_RATE)
    sections = []
    if len(song) > section_frames * 2:
        starts = np.linspace(0, len(song) - section_frames, SECTIONS).astype(int)
        for start in starts:
            lag, score = best_lag(song[start : start + section_frames], video)
            sections.append((start / ENVELOPE_RATE, (lag - start) / ENVELOPE_RATE, score))

    return overall_lag / ENVELOPE_RATE, overall_score, sections


def find_map(query):
    matches = [m for m in cinema_sync.list_maps() if query.lower() in Path(m[0]).name.lower()]
    if not matches:
        sys.exit(f"No map on the Quest matches '{query}'")
    if len(matches) > 1:
        names = "\n  ".join(Path(m[0]).name for m in matches)
        sys.exit(f"'{query}' matches several maps, be more specific:\n  {names}")
    return matches[0]


def song_file_name(level_dir):
    raw = cinema_sync.adb("exec-out", "cat", f"{level_dir}/Info.dat", check=False).stdout
    try:
        info = json.loads(raw.decode("utf-8-sig"))
    except (json.JSONDecodeError, UnicodeDecodeError):
        info = {}
    # v2 Info.dat uses _songFilename, v4 uses audio.songFilename
    return info.get("_songFilename") or info.get("audio", {}).get("songFilename") or "song.egg"


def video_audio(config, local_video):
    if local_video:
        if not has_audio(local_video):
            sys.exit(f"{local_video} has no audio track")
        return Path(local_video)

    video_id = config.get("videoID")
    if not video_id:
        sys.exit("The config has no videoID, pass the video file with --video")
    # Videos from cinema_sync.py are video only, so look for an audio file or a cached video that has audio
    for cached in sorted(cinema_sync.CACHE_DIR.glob(f"{video_id}*")):
        if cached.suffix != ".part" and has_audio(cached):
            return cached
    if not shutil.which("yt-dlp"):
        sys.exit("yt-dlp is not installed, pass the video file with --video")
    print("Downloading the video's audio...")
    output = cinema_sync.CACHE_DIR / f"{video_id}.audio.%(ext)s"
    cinema_sync.CACHE_DIR.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["yt-dlp", "--no-playlist", "-f", "ba/b", "-o", str(output), f"https://www.youtube.com/watch?v={video_id}"])
    downloaded = [p for p in cinema_sync.CACHE_DIR.glob(f"{video_id}.audio.*") if p.suffix != ".part"]
    if result.returncode != 0 or not downloaded:
        sys.exit("Couldn't download the video's audio")
    return downloaded[0]


def save_offset(level_dir, config, offset_ms):
    config = {**config, "offset": offset_ms}
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(config, f, indent=2)
    pushed = cinema_sync.adb("push", f.name, f"{level_dir}/{cinema_sync.CONFIG_FILENAME}", check=False).returncode == 0
    Path(f.name).unlink()
    return pushed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("map", help="part of the map's folder name on the Quest")
    parser.add_argument("--video", help="local video file with audio, instead of downloading the audio")
    parser.add_argument("--apply", action="store_true", help="save the offset to the map's cinema-video.json")
    args = parser.parse_args()

    for tool in ("adb", "ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is not installed")

    level_dir, _, config = find_map(args.map)
    from_bundle = False
    if config is None:
        sha1 = cinema_sync.load_level_hashes().get(level_dir)
        config = cinema_sync.load_bundled_configs().get(f"custom_level_{sha1}") if sha1 else None
        from_bundle = config is not None
    if config is None:
        sys.exit("This map has no Cinema config yet. Add one on the upload page first, so the script knows which video to use.")
    print(f"Map: {Path(level_dir).name}\nVideo: {config.get('title') or config.get('videoID')}")

    with tempfile.TemporaryDirectory() as tmp:
        song_path = Path(tmp) / song_file_name(level_dir)
        with open(song_path, "wb") as f:
            f.write(cinema_sync.adb("exec-out", "cat", f"{level_dir}/{song_path.name}").stdout)
        song_samples = decode(song_path)
    video_samples = decode(video_audio(config, args.video))
    print(f"Song {len(song_samples) / SAMPLE_RATE:.1f}s, video audio {len(video_samples) / SAMPLE_RATE:.1f}s. Matching...")

    offset, score, sections = find_offset(song_samples, video_samples)
    offset_ms = int(round(offset * 1000))

    print()
    for section_start, section_offset, section_score in sections:
        print(f"  section at {section_start:5.1f}s: offset {section_offset * 1000:7.0f} ms (match strength {section_score:.2f})")
    agreeing = [s for s in sections if abs(s[1] - offset) * 1000 <= AGREEMENT_MS]

    print(f"\nOffset: {offset_ms} ms (current config: {config.get('offset', 0)} ms)")
    # A peak that barely beats the next best match, or sections that disagree, mean the result is a guess
    reliable = score >= 1.15 and (not sections or len(agreeing) >= max(2, len(sections) // 2 + 1))
    if reliable:
        print(f"Confident: {len(agreeing)} of {len(sections)} sections agree.")
    else:
        print("Not confident: the video may be a different edit of the song (extra intro, cut verses, different tempo).")
        print("Check it with the Cinema tab in game before relying on it.")

    if args.apply:
        if not reliable and input("Save it anyway? [y/N] ").strip().lower() != "y":
            return
        if save_offset(level_dir, {**config, "bundledConfig": True} if from_bundle else config, offset_ms):
            print("Saved. It applies from the next time you start the song.")
        else:
            print("Couldn't save the config to the Quest.")


if __name__ == "__main__":
    main()
