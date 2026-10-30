# Taking Stock Install
### Last Updated by Tench Cholnoky 10.16.2026

## INTRO

This app plays tiled layouts of videos from the original [takingstock](https://github.com/mandiberg/takingstock) project. It fills the window with arrangements of those videos, and can play a matching audio file for each arrangement.

From this folder, `make` builds the app the first time. After that, `make RunRelease` starts it. The first launch can sit for a few minutes while it builds valid arrangements. The terminal prints aspect ratios and how many arrangements it generated before the window opens.

Folders you will edit:

- `videos/` — video files and `installation.csv`
- `audio/` — audio files matched to those videos
- `config/` — machine settings and window templates
- `python_utils/` — scripts for stand-in audio and rescaling videos

## SETUP

This repo does not work with an unmodified openFrameworks install. It needs the 0.12.1 macOS release, plus one replaced source file.

1. Download [of_v0.12.1_osx_release](https://openframeworks.cc/download/) and unzip that folder into `openframeworks/`. Keep the folder name exactly `of_v0.12.1_osx_release`. Do not drop in the zip, and do not move only the files inside it.
2. Replace the SDK's video player with the copy in this repo. From this folder:

```
cp openframeworks/ofAVFoundationVideoPlayer.m openframeworks/of_v0.12.1_osx_release/libs/openFrameworks/video/
```

You can also copy the file by hand into `of_v0.12.1_osx_release/libs/openFrameworks/video/`.

3. Put the video files and `installation.csv` in `videos/`. See VIDEOS.
4. Put the audio files in `audio/`. See AUDIO.
5. Open a terminal in this repo and build:

```
cd (path to this repo)
make
make RunRelease
```

`make` is only required the first time, or after the C++ sources change. Expect 1–2 minutes and a lot of compiler output. Later sessions can start with `make RunRelease`.

When running make for the first time on a new device, you may be prompted to install Command Line Tools. Follow this dialog box's instructions. FYI, your terminal may output this error:

xcode-select: note: No developer tools were found, requesting install.
If developer tools are located at a non-default location on disk, use `sudo xcode-select --switch path/to/Xcode.app` to specify the Xcode that you wish to use for command line developer tools, and cancel the installation dialog.
See `man xcode-select` for more details.


If you are on an unstable internet connection or struggling to have the app run, you can download the Command Line Tools package on another device and transfer the .dmg to the computer you are installing this on and double click the .dmg to install the software. 

Once the Command Line Tools package has been installed, try running make again. The inital run of make on a new device will print out lots of warnings that you can disregard as long as it compiles and prints this message at the end:

     compiling done
     to launch the application

     cd bin/taking-stock-install.app/Contents/MacOS/
     ./taking-stock-install
     
     - or -
     
     make RunRelease

> **If `make` dies with `fatal error: 'map' file not found`, go to [Troubleshooting 1A](#1a-xcode-install-errors-during-make).**

## VIDEOS

`videos/` is where the app looks for playback files. It needs the mp4s and a csv named `installation.csv` in that same folder.

The csv is produced in the takingstock repo by `make_video.py` (mode 1) and `merge_expanded_videos.py`. It needs these columns:

`cluster_no`, `hsv_no`, `pose_no`, `width`, `height`, `ratio`, `file_name`, `duration`, `object`

`file_name` is the video file sitting next to the csv. `cluster_no` is also what the app uses to find audio. `width`, `height`, and `ratio` are what the arrangement generator uses to tile the window. `object` is a list of object ids for that video, such as `[]` or `[82]`. When **[config option]** - SELECT_MODE is on, the app uses this column to choose which videos can appear.

## AUDIO

`audio/` holds one file per cluster you want to hear. The app reads the key video's `cluster_no` from `installation.csv` and plays the first file in this folder whose name contains that string. If nothing matches, that arrangement is silent.

Name files so the cluster id is in the filename. Matching, fade, surround, and channel settings are described under AUDIO in [config/configREADME.md](config/configREADME.md).

If setting up audio with the Scarlette 4i4 from Focusrite, install Focusrite Control 2 from https://downloads.focusrite.com/focusrite/scarlett-4th-gen/scarlett-4i4-4th-gen

Then go to "Audio MIDI Setup" and select the Audio Interface from the left menu. Click configure speakers in the bottom right and select Quadrophonic from the Configuration dropdown. Click apply and return to the FocusRite software. In the bottom left of FocusRite, load the Taking_Stock_Quad preset, which can be found in the utilities folder. This preset will map each channel of audio to a playback channel in Mixer A or Mixer B. These channels will be hard panned, one for each direction in either mixer. Sliders within these mixers will control audio gain for each channel. 

## ARRANGEMENTS

An arrangement is one full-window layout. In a 1000×1000 window, one arrangement might be a 500×1000 vertical video beside two 500×500 squares. The app builds these with a bin-packing pass. A valid arrangement fills the window with no gaps.

Generated files are saved in `arrangements/`. The name looks like this:

`1.333_s3fa9c2d1_arrangements_393`

That is aspect ratio (width/height), then `s` plus a hash of the generation settings, then how many arrangements are in the file.

The hash comes from the generation settings in the selected window config and from **[config option]** - RATIO_ROUND_DECIMALS in `config/system_config.txt`. On startup the app reuses a file only when both the aspect ratio and the hash match. The current hash is printed in the startup log. Which settings are included is listed under ARRANGEMENTS CACHE in [config/configREADME.md](config/configREADME.md).

To force a new set, delete the arrangement file for that aspect ratio. When a new set is saved, every other arrangement file for the same aspect ratio is deleted. Files for other aspect ratios are left alone.

You may also see a file like `1.333_arrangements_inputs.fingerprint`. That file is used only when **[config option]** - IGNORE_FINGERPRINT is false. It records the state of `installation.csv` and the video files so arrangements are rebuilt when the videos change. The cleanup above does not delete it.

## CONFIG

All settings live in `config/`. `config/system_config.txt` is always loaded. Its **[config option]** - WINDOW_CONFIG line picks the window template. Shipped templates are in `config/canonical_configs/`. Your own variations go in `config/custom_configs/`. If the same setting appears in both files, `system_config.txt` wins.

The app reads config on every launch, so a settings change needs a restart, not a rebuild.

Every option is documented in [config/configREADME.md](config/configREADME.md).

## PYTHON UTILS

Scripts in `python_utils/` are optional. They prepare media. They are not part of `make`.

- `python_utils/fix_missing_audio_standins.py` reads `installation.csv`. For a cluster that has no matching audio file, it copies a template wav into the audio folder and sets that row's `audio_copy` column.
- `python_utils/scale_to_input_height.py` asks for a display height in pixels, then scales videos with ffmpeg and writes a new `installation.csv` into `scaled_videos_<height>/`. `--dry-run` skips encoding. `--limit N` encodes only the first N videos. The input folder and csv path are set at the top of the script (`../TheGymVideos/videos`).

## TROUBLESHOOTING

### 1A. Xcode install errors during `make`

`make` compiles the openFrameworks library first. If the macOS Command Line Tools are installed but broken, that compile stops immediately:

```
fatal error: 'map' file not found
#include <map>
```

`make` then exits with `Error 2`. The header is part of the C++ standard library, so this is a tools install problem, not a missing file in this repo.

Remove the Command Line Tools and install them again:

```
sudo rm -rf /Library/Developer/CommandLineTools
xcode-select --install
```

Confirm the install dialog, wait until it finishes, then run `make` again from this repo.
