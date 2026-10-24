<div align="center">
<h1>Cinema for Quest</h1>
<p>A port for the Cinema mod on beatsaber quest</p>
</div>

Watch a songs music video on a giant screen behind the notes while you play Beat Saber on Quest.

This is a Quest port of [Cinema](https://github.com/Kevga/BeatSaberCinema) by Dakari, the popular PC mod. It uses the same video configs, so maps that support Cinema on PC work here too.

**Works with:** Beat Saber **1.40.8** (1.40.8_7379) on Quest, modded with [ModsBeforeFriday (MBF)](https://mbf.bsquest.xyz/).

---

## Contents

- [Installing](#installing)
- [How it works](#how-it-works)
- [Getting videos onto your Quest](#getting-videos-onto-your-quest)
- [Adding a video to a map that doesnt have one](#adding-a-video-to-a-map-that-doesnt-have-one)
- [Getting the timing right](#getting-the-timing-right)
- [Settings](#settings)
- [Troubleshooting](#troubleshooting)
- [Differences from PC Cinema](#differences-from-pc-cinema)
- [Building from source](#building-from-source)

---

## Installing

1. Get `Cinema.qmod` from releases.
2. Open [MBF](https://mbf.bsquest.xyz/) with your Quest connected, and drag `Cinema.qmod` onto the page (or use **Upload mods**).
3. MBF installs Cinema and anything it needs (BSML, SongCore and friends) automatically.
4. Start Beat Saber. Youll find **Cinema** under **Settings → Mod Settings**, and a **Cinema** tab next to *Modifiers* when choosing a song.

---

## How it works

A map gets a video when two things are in its folder:

1. **A video config** (`cinema-video.json`). This says which YouTube video goes with the song, and how far into the video it should be when the song starts (offset). It can also move the screen and change the environment around it.
2. **The video file** itself (an `.mp4`).

Where configs come from:

- **The mapper.** Some maps on BeatSaver come with a `cinema-video.json` already.
- **Built in.** Cinema knows the right video for about 110 popular custom maps, the same list PC Cinema has. You dont need to do anything for those, just add the video file.
- **You.** You can make your own for any map, see [Adding a video to a map that doesnt have one](#adding-a-video-to-a-map-that-doesnt-have-one).

When you start a song that has both, the video plays on a screen in the environment, in time with the music. If the config asks for it, Cinema also switches to a different environment or hides parts of it so the screen is easier to see.

**Why do I have to add the video file myself?** Quest cant download YouTube videos by itself, so the video has to come from another device (your PC or phone). The next section shows two easy ways.

---

## Getting videos onto your Quest

### Option 1: The upload page (any PC or phone, no cable)

Cinema can show a web page you open from a PC or phone on the same Wi-Fi as your Quest.

1. In Beat Saber, go to **Settings → Mod Settings → Cinema** and turn on **Enable video upload page**.
2. Underneath, it shows an address like `http://192.168.0.101:2468`. Type that into a browser on your PC or phone.
3. The page lists the maps on your Quest:
   - **Missing video:** maps with a config but no video yet.
   - **Has video:** maps that are ready to go.
   - **No Cinema config:** everything else (see [Adding a video](#adding-a-video-to-a-map-that-doesnt-have-one)).
4. For a map thats missing its video, download the video on your PC or phone. The page gives you a link to the YouTube video, plus a **Copy yt-dlp command** button if you use yt-dlp
5. Click **Upload video**, pick the file, and wait for the progress bar. Done.

For those using yt-dlp I use the command
```bash
yt-dlp -f "bestvideo[height<=720][ext=mp4]" --no-audio "https://www.youtube.com/watch?v=8XkbL0JzMbk"
```
This downloads the video in 720p with no audio, the screen is far so no requirement for 1080p a lot of the time

Keep Beat Saber open while you use the page. The upload page is off by default, because while its on, anyone on your Wi-Fi can open it. Turn it off again when youre done.

> **Tip:** Cinema doesnt play the videos sound (you hear the song itself), so a video without audio is fine and smaller.

### Option 2: The sync script (PC or Mac, with a cable)

If youre comfortable with a terminal, the sync script does everything in one go: it finds every map on your Quest thats missing its video, downloads them and copies them over.

You need [Python 3](https://www.python.org/), [adb](https://developer.android.com/tools/releases/platform-tools) (with your Quest in developer mode and connected by USB), and [yt-dlp](https://github.com/yt-dlp/yt-dlp) (recent versions also need [Deno](https://deno.com/) installed to download from YouTube).

```sh
python3 tools/cinema_sync.py --dry-run    # see what it would download
python3 tools/cinema_sync.py              # download and copy everything missing
python3 tools/cinema_sync.py "song name"  # song name doesnt need to be exact just close enough and it will download just that one
```

Videos are downloaded in 1080p by default. Add `--max-height 720` for smaller files.

---

## Adding a video to a map that doesnt have one

1. Open the [upload page](#option-1-the-upload-page-any-pc-or-phone-no-cable).
2. Find the map under **No Cinema config** (the search box helps) and click **Add video**.
3. Paste the YouTube link of the music video. Any link format works.
4. Leave **Offset** at 0 for now, and click **Add video**.
5. Upload the video file for that map, the same way as above.
6. Now get the timing right, see the next section.

You can change a maps video or offset later with **Edit config**. If you change the link on a map that already has a video, the old video file is renamed to `.old` (not deleted) and youll need to upload the new one.

---

## Getting the timing right

Music videos often have an intro, so the music in the video doesnt start at the same moment as the song. The **offset** fixes that: its how far into the video (in milliseconds, 1000 ms = 1 second) Cinema should be when the song starts.

- If the music in the video starts **4 seconds later** than in the song, the offset is **4000**.
- If the video should start **after** the song has begun, the offset is negative.

Maps with a mappers or a built-in config already have the right offset. For configs you made yourself, there are two ways to find it.

### Automatically (PC or Mac)

The offset finder listens to the song and the video and works out where they line up:

```sh
python3 tools/cinema_offset.py "Blame Canada"            # show the offset
python3 tools/cinema_offset.py "Blame Canada" --apply    # and save it
```

Use any part of the maps name. It needs the same tools as the sync script, plus [ffmpeg](https://ffmpeg.org/). The first time, it sets up a small helper environment, which takes a minute.

It checks several parts of the song separately. If they agree it says **Confident**. If they dont, the video is probably a different version of the song (for example an extended intro or a cut verse), and it tells you to check by eye.

### By eye, in game

1. Choose the song in Beat Saber and open the **Cinema** tab next to *Modifiers*.
2. Set **Snippet start** to a part of the song thats easy to recognise, like the start of the chorus or a big beat drop.
3. Press **Play**. That part of the song plays, with the video on a screen in front of you.
4. Watch and listen. If the video is **behind** the music, press the **+** buttons. If its **ahead**, press **−**. Changes apply straight away while it plays.
   - Use **±1000** to get close, then **±100** and **±10** to fine-tune.
5. When it looks right, press **Save**. **Revert** goes back to the last saved offset.

---

## Settings

**Settings → Mod Settings → Cinema**

| Setting | What it does |
|---|---|
| Enable Cinema | Turns videos on or off. |
| Override environment | Lets Cinema switch to an environment that suits a video better when the maps config doesnt choose one. |
| Disable all environment overrides | Never change the environment, even if a config asks for it. |
| Enable video upload page | Turns the [upload page](#option-1-the-upload-page-any-pc-or-phone-no-cable) on or off, and shows its address. |
| Screen adjustments | Move the screen left/right, up/down and closer/farther, or make it bigger or smaller. This applies on top of each maps own placement. **Reset screen adjustments** undoes it. |

---

## Troubleshooting

**The video doesnt show up.**
Check the upload page: the map should be under *Has video*. With a negative offset the screen only appears once the song reaches the start of the video, so give it a moment. Changes apply from the next time you start the song.

**The video is out of sync.**
Find the right offset with the [offset finder or the Cinema tab](#getting-the-timing-right). If it starts in sync but drifts during the song, the video is probably a different edit of the song than the map uses. Try a different upload of the video on YouTube.

**I cant open the upload page.**
Make sure *Enable video upload page* is on, Beat Saber is open, and your PC or phone is on the same Wi-Fi network as the Quest. Some guest or public networks block devices from seeing each other.

**The screen is in an odd place or too big.**
Use the *Screen adjustments* in settings. Configs made for PC sometimes expect a different environment, so *Override environment* being on helps them look as intended.

**Logs**
Cinema writes a log to `ModData/com.beatgames.beatsaber/logs2/cinema.log` on your Quest, which is useful when reporting a problem.

---

## Differences from PC Cinema

- Videos cant be searched for or downloaded in game. Use the [upload page or sync script](#getting-videos-onto-your-quest) instead.
- The video screen is flat and plain. PCs curved screen, vignette, color correction, fades and bloom arent ported yet.
- Not yet supported: object cloning in configs (`cloneFrom`), additional screens, 360° levels (the screen doesnt follow rotation), official (OST) songs, and multiplayer.
- New on Quest: the upload page, the in-game offset tuner, screen position adjustments, and the offset finder.

---

## Building from source

You need [qpm](https://github.com/QuestPackageManager/QPM.CLI), CMake, Ninja, PowerShell and Android NDK 27.

```sh
qpm restore
qpm ndk resolve        # or put your NDK path (no spaces) in ndkpath.txt
pwsh ./scripts/build.ps1
qpm qmod manifest      # generate mod.json
pwsh ./scripts/createqmod.ps1 Cinema
```

For quick testing, push `build/libcinema.so` to `/sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/` and restart the game.

---

## Credits and license

Cinema was created by [Dakari](https://github.com/Kevga) ([BeatSaberCinema](https://github.com/Kevga/BeatSaberCinema)). 
Claude for the README and /tools
This port includes its video configs and is based on its code. Like the original, its licensed under the [GNU GPL v3](LICENSE).
