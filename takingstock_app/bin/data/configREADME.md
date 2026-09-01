# CONFIG README

### Last updated by Tench C 05.26.26

The config.txt file is where all config of the takingstock_app happens. Each line is used for a different setting, and lines that begin with a # indicate a comment line. This README is a guide to what each specific config option does.

## WINDOW

`BOX_WIDTH` and `BOX_HEIGHT` define both the render canvas size and the OS window size. The window is created borderless and non-resizable at startup — resizing it after launch is not supported.

**BOX_WIDTH** = Width in pixels of the render canvas and OS window. For a single display set this to its native width. For multiple side-by-side projectors or monitors, set this to the **combined** width of all displays (e.g. two 1920-wide projectors = `3840`). (default = 1920)

**BOX_HEIGHT** = Height in pixels of the render canvas and OS window. For a single display set this to its native height. For side-by-side projectors that share the same height, this is just that shared height (e.g. `1080` for standard 1080p projectors). (default = 1080)

**WINDOW_X** = X position of the window's top-left corner on the macOS extended desktop, in pixels. In **System Settings → Displays → Arrange**, the primary display's top-left is always `(0, 0)`. A second monitor arranged to the **right** of the primary starts at `x = <primary width>` (e.g. `1920` for a 1920-wide primary). A second monitor arranged to the **left** starts at a negative value (e.g. `-1920`). Set to `0` to open on the primary display. (default = 0)

**WINDOW_Y** = Y position of the window's top-left corner on the macOS extended desktop. Usually `0` unless monitors are stacked vertically or offset. (default = 0)

**WINDOW_DECORATED** = Options: [true, false] When `false`, the window opens without a title bar or borders — the correct mode for installation. When `true`, the window has a normal macOS title bar, which is useful for development and debugging. (default = false)

### Multi-projector setup

macOS treats multiple displays in Extended mode as one continuous coordinate space. A single window can span all of them by sizing it to the combined resolution and positioning it at the correct desktop offset. No secondary software is needed.

> **Required macOS setting:** Multi-display spanning only works when **"Displays have separate Spaces"** is turned **off**. When it is on, macOS clips windows to whichever display they start on and prevents them from spanning to adjacent displays. To turn it off: **System Settings → Desktop & Dock → scroll to the bottom → toggle off "Displays have separate Spaces"**. A log out and back in is required for the change to take effect.

**Steps:**
1. Turn off **"Displays have separate Spaces"** as described above and log out/in.
2. In **System Settings → Displays → Arrange**, set all displays to **Extended** (not Mirrored). Note which side each display is on relative to the primary.
3. Set `BOX_WIDTH` to the total combined width and `BOX_HEIGHT` to the shared height.
4. Set `WINDOW_X` to the x offset where the leftmost display in your span begins (usually `0`).
5. Rebuild and run — macOS automatically routes the correct pixel region to each physical output.

**Common configurations:**

| Layout | BOX_WIDTH | BOX_HEIGHT | WINDOW_X | WINDOW_Y |
|--------|-----------|------------|----------|----------|
| Single 1920×1080 display | 1920 | 1080 | 0 | 0 |
| Two 1920×1080, second to the right | 3840 | 1080 | 0 | 0 |
| Two 1920×1080, second to the left | 3840 | 1080 | -1920 | 0 |
| Three 1920×1080, primary in center | 5760 | 1080 | -1920 | 0 |



## SECONDARY WINDOW

The secondary window is an optional second OS window that displays live data about the current arrangement's key video. It is intended to eventually show spreadsheet-style metadata; for now it displays the key video's `cluster_no` value, updated automatically on every arrangement change.

The secondary window requires `KEY_VIDEO = true` to be meaningful. If the secondary window is enabled but `KEY_VIDEO` is disabled, the window will display a message indicating that `KEY_VIDEO` must be enabled. When `KEY_VIDEO` is enabled but no qualifying video is found for the current arrangement, the window shows `--` in place of the cluster number.

The secondary window shares a GL context with the main window and runs on the same loop — no additional threads are involved.

**SECONDARY_WINDOW_ENABLED** = Options: [true, false] When `true`, a second OS window opens alongside the main render window at startup. When `false`, no secondary window is created and the other `SECONDARY_WINDOW_*` settings are ignored. (default = false)

**SECONDARY_WINDOW_WIDTH** = Width in pixels of the secondary window. (default = 400)

**SECONDARY_WINDOW_HEIGHT** = Height in pixels of the secondary window. (default = 300)


## PATHS

**VIDEO_ASSET_PATH** = This is a path to the folder which contains all the video files that will be used in the render
**VIDEOS_CSV_PATH** = This is a path to the csv file which contains all the information about the video files that will be used in the render
**ARRANGEMENTS_PATH** = This is a path to the folder where all generated arrangements will be saved.
**AUDIO_PATH** = This is a path to the folder which contains all the audio files used for playback. The app matches each arrangement's key video `cluster_no` value against filenames in this folder — any file whose name contains the `cluster_no` string is used as that arrangement's audio.



## LOOPS

**VIDEO_LOOP** = Options: [true, false] This decides whether videos will loop when finished or be replaced with another video of the same aspect ratio (default = true) **SET TO TRUE WHEN RUNNING FOR >12HRS**

**CYCLE_RESET_DURATION** = Duration in seconds to hold a black screen when a cycle reset fires (see `CYCLE_RESET_COUNT`). The app fades/cuts to black normally as part of the transition to the next arrangement, then holds black for this duration before resuming. This gives AVFoundation time to complete any pending async video teardowns that have accumulated, acting as a periodic cleanup to maintain long-run stability. Set to `0` to disable the black hold even if `CYCLE_RESET_COUNT` is set. The hold occurs seamlessly — the next arrangement's videos are already loaded and ready when black clears. (default = 5)

**CYCLE_RESET_COUNT** = Number of arrangements to display before triggering a cycle reset (the black-screen hold controlled by `CYCLE_RESET_DURATION`). For example, `CYCLE_RESET_COUNT = 20` triggers a reset every 20 arrangements shown. Set to `0` to disable the cycle reset entirely. Arrangements are now picked randomly from the full pool on each transition with no forced ordering, so this count-based trigger replaces the old behavior of resetting only after every arrangement had been shown exactly once. (default = 0)

**MIN_VIDEO_LENGTH** = Minimum duration in seconds a video must have to be accepted into the pool. Any video in the CSV with a duration shorter than this value — including videos with no duration data — is discarded at load time and will never appear in any arrangement. Set to `0` to keep all videos regardless of length. (default = 0)

**SCALE_SELECT** = Options: [true, false] When `true`, the app uses the `width` and `height` columns in `installation.csv` together with the `_scale_<N>` suffix in each filename to group scale variants of the same video. At slot-assignment time, it picks the **smallest scale variant whose pixel dimensions still meet or exceed the slot's pixel dimensions** — for example, a 1500×1000 slot selects the `scale_1080` file (1624×1080) rather than the larger `scale_1712` or `scale_2160` files. This reduces GPU scaling load at runtime: the video is decoded at a resolution close to how it will be drawn rather than at a much higher resolution that gets scaled down in real time. All scale variants of the same logical video are treated as a single entry in the available pool, so the same source content cannot appear more than once in one arrangement at different scales.

When `false`, every row in `installation.csv` is treated as an independent video and the `width`/`height` columns are ignored for selection purposes — this is identical to the behavior before this option was added. (default = false)

> **Requires:** `installation.csv` must have `width` and `height` columns, and video filenames must include a `_scale_<digits>` segment before the `.mp4` extension (e.g. `...ct8_scale_1080.mp4`, `...ct8_scale_1712.mp4`). Videos without a `_scale_` suffix in the filename are always treated as their own unique logical video and will never be grouped with other files.



## CONTROLS
**s**: export image 
**r**: regenerate layout


## KEY VIDEO

The key video is the longest-playing video in an arrangement. When key video mode is enabled, the layout transition fires when the key video's full duration has elapsed, instead of using the random `TRANSITION_TIMER_MIN/MAX` range. This ensures the arrangement is always held for exactly as long as its longest video plays.

**KEY_VIDEO** = Options: [true, false] When `true`, the transition timer is set to the duration of the longest loaded video that meets the minimum length requirement. If no qualifying video is found, the app falls back to the random `TRANSITION_TIMER_MIN/MAX` timer. (default = true)

**KEY_VIDEO_MIN_LENGTH** = The minimum duration in seconds a video must have to be considered the key video. For example, setting this to `30` means only videos 30 seconds or longer can be the key video. Set to `0` to allow any video to qualify. (default = 20)


## TRANSITIONS

Transitions are how the app moves between arrangements. There are three main modes: **jumpcut**, **fade**, and **jumpcut_to_black**. 
- in **jumpcut** mode, arrangements will immediately cut from arrangement A to B.
- in **fade** mode, arrangements will fade to black from arrangement A for a set number of seconds (*TRANSITION_DURATION_FADE*) and then fade up to arrangement B for a (*TRANSITION_DURATION_FADE*) number of seconds.
- in **jumpcut_to_black** mode the arrangement will cut from A to black for a set number of seconds (*TRANSITION_DURATION_JUMP_TO_BLACK*) and then cut to arrangement B

Each arrangement is held for a random amount of time between TRANSITION_TIMER_MIN and TRANSITION_TIMER_MAX

**TRANSITION_TYPE** = Options: [**jumpcut**, **fade**, **jumpcut_to_black**]
**TRANSITION_DURATION_FADE** = this is the duration in seconds of each part of the fade in the **fade** transition type, meaning it is the duration of the fade down, and then again the fade up, so the full transition will take 2x this number.
**TRANSITION_DURATION_JUMP_TO_BLACK** = this is the duration in seconds to hold the black screen in the **jumpcut_to_black** transition mode
**TRANSITION_TIMER_MIN** = This is the minimum duration in seconds for each arrangement to be held
**TRANSITION_TIMER_MAX** = This is the maximum duration in seconds for each arrangement to be held



## AUDIO

Each arrangement plays one audio file tied to its key video. The app reads the key video's `cluster_no` value from `installation.csv`, then searches `AUDIO_PATH` for any file whose name contains that string. The first match is played looping for the duration of the arrangement. If no matching file is found, the arrangement plays silently.

Audio transitions follow `TRANSITION_TYPE`:
- **jumpcut** / **jumpcut_to_black**: audio cuts immediately — the old file stops and the new one starts at full volume
- **fade**: audio fades out at the start of the visual fade, and the new audio fades in from silence once the screen is black

**AUDIO_FADE_DURATION** = Duration in seconds for the audio fade in and fade out during a **fade** transition. Set to `0` for an instant cut even when using the fade visual transition. (default = 1.0)

**AUDIO_SURROUND** = Options: [true, false] When `true`, the audio engine routes output through a quad (4-channel) layout (`kAudioChannelLayoutTag_Quadraphonic`). The audio file must have at least 4 channels; if it has fewer, the app logs a warning and falls back to the file's native format automatically. When `false`, audio plays in the file's native stereo (or mono) format. (default = false)

**AUDIO_CHANNEL_MAP** = Defines, for each output channel position, which **source file channel** to read from (0-based index). This lets you reorder channels without re-exporting the audio file. The number of entries should match the output channel count (2 for stereo, 4 for quad). Entries beyond the output channel count are ignored. If this setting is omitted or left empty, a pass-through identity map is used (output channel N reads from source channel N).

The quad output channel order when `AUDIO_SURROUND = true` follows the WAV/FFmpeg standard:

| Output index | Speaker | Abbreviation |
|---|---|---|
| 0 | Front Left | FL |
| 1 | Front Right | FR |
| 2 | Back Left | BL |
| 3 | Back Right | BR |

The clockwise room cycle (FL → FR → BR → BL) maps to indices `0, 1, 3, 2`.

**Example** — swap back left and back right: `AUDIO_CHANNEL_MAP = [0, 1, 3, 2]`

**AUDIO_CHANNEL_GAINS** = Per-output-channel gain multiplier. Each value is applied to the corresponding output channel's samples at load time (not in real time), so there is no CPU overhead during playback. Values range from `0.0` (silence) to `1.0` (full). Values above `1.0` are technically valid but may clip. The number of entries should match the output channel count; entries beyond that are ignored. If omitted, all channels play at gain `1.0`.

**Example** — pull back the rear speakers slightly: `AUDIO_CHANNEL_GAINS = [1.0, 1.0, 0.8, 0.8]`

**AUDIO_DEVICE** = The exact name of the audio output device to target, as shown in **Audio MIDI Setup** (open via Spotlight → "Audio MIDI Setup"). Leave empty to use the macOS system default output device. If the named device is not found at startup, the app logs a warning and falls back to the system default without crashing. The device must be configured to the correct channel count in Audio MIDI Setup before the app starts — for quad output this means the device must be set to a 4-channel format.

> **Quad setup checklist:**
> 1. Connect your 4-channel interface or amplifier.
> 2. Open **Audio MIDI Setup**, select the device, and set the format to 4ch / your sample rate.
> 3. Verify speaker assignments in the **Configure Speakers** sheet (FL, FR, BL, BR).
> 4. Set `AUDIO_DEVICE` to the exact device name shown (e.g. `Focusrite USB ASIO`).
> 5. Set `AUDIO_SURROUND = true` and configure `AUDIO_CHANNEL_MAP` / `AUDIO_CHANNEL_GAINS` as needed.
> 6. Provide audio files with 4 channels (WAV/FFmpeg quad order: FL FR BL BR) in the `AUDIO_PATH` folder.



## SELECT MODE

Select mode allows you to filter which videos are used in arrangements based on the `objects` column in `installation.csv`. The `objects` column is expected to hold a Python-style list of integer IDs, e.g. `[67]`, `[67, 95]`, or `[]` for no detected objects. You can define multiple filter options each with a probability weight, and the app randomly picks one filter at each transition. This lets you bias arrangements toward specific object classes (e.g. only show videos tagged with object `67`) or mix them at weighted probabilities.

**SELECT_MODE** = Options: [true, false] When `true`, video selection is filtered according to the `SELECT` lines below. Requires an `objects` column in the CSV — if none is found, select mode is automatically disabled. (default = false)

**SELECT_EXACT_MATCH** = Options: [true, false] Controls how a video's object list is matched against a `SELECT` filter. (default = false)
- `false` (**any** mode): a video passes if its object list contains **at least one** of the IDs in the SELECT filter.
- `true` (**exact** mode): a video passes only if its object list is **exactly equal** to the SELECT filter's list (same IDs, any order, no extras).

| Mode | SELECT | CSV `objects` | Passes? |
|------|--------|---------------|---------|
| any  | `[67]` | `[67, 95]` | Yes — 67 found in list |
| any  | `[67]` | `[95]` | No |
| exact | `[67, 95]` | `[67, 95]` | Yes — sets equal |
| exact | `[67, 95]` | `[67, 95, 100]` | No — extra element |
| exact | `[67]` | `[67, 95]` | No — extra element |
| either | `[]` | `[]` | Yes — empty list match |
| either | `[*]` | anything | Yes — wildcard |

**SELECT** = Defines one filter option. Format: `SELECT = [obj1, obj2, ...], weight`
- The object list is matched against the parsed `objects` column values using the mode set by `SELECT_EXACT_MATCH` (see above).
- Use `[*]` to allow any video regardless of its object list (equivalent to no filter).
- Use `[]` (empty brackets) to select only videos whose object list is empty (i.e. no detected objects).
- `weight` is a relative probability — a weight of `0.3` alongside two other `0.3` entries means each has a 1-in-3 chance of being picked per transition.
- Multiple `SELECT` lines are allowed; one is chosen randomly at each transition using the weights.



## ARRANGEMENTS

Arrangements is the term used to describe the different layouts the app generates and saves based on the aspect ratios of the videos and installation.csv file in the videos folder. For instance, in a 1000x1000px render window, one arrangement might be a 500x1000 vertical video and two 500x500 square videos. Arrangements are generated based a modified bin sorting algorithm. the config file has options for controlling how arrangements are generated and which ones are accepted as valid, but generally all valid arrangements will fill the entire window with no empty space and tile perfectly.  

Arrangement generation happens with a variety of paramaters that dictate how long arrangement generation lasts for and what arrangements are considered valid. Arrangement generation happens at program start, unless there is already an arrangements file that matches all the settings and videos in the arrangements folder. 

Arrangements decide which videos to pick based on two factors, scale (area in pixels), and weight (calculated per aspect ratio by how many videos in the videos folder have that specific aspect ratio). 
If there are 20 videos with a 2x3 aspect ratio and 5 videos with a 1x1 aspect ratio, the 2x3 aspect ratio will have a weight 4x what the 1x1 aspect ratio will have. Both weight and scale are used to decide which videos are placed in arrangements.



### ARRANGEMENTS ATTEMPTS
Arrangement generation happens in a phase based system, where the program attempts to generate a certain amount of arrangements, and if they are considered valid (we'll get to that in the next step) it saves them. If it creates creates too many duplicates and stalls out it will move to the next phase, which uses a seeding system to try to fill in some different spaces. 

Attempts go like this -> 
generate until either **LAYOUT_MAX_ATTEMPTS** number of generations reached or a **LAYOUT_STALE_THRESHOLD** number of duplicate arrangements created, then re-seed, go to the next phase, and repeat until all phases completed.

**LAYOUT_MAX_ATTEMPTS** = This is the maximum number of generation attempts the program will try per phase (default = 50000)
**LAYOUT_STALE_THRESHOLD** = This is the maximum number of duplicates the program will allow before it skips to the next phase (default = 3000)
**LAYOUT_PHASES** = This is the number of reseeded phases the program will complete (default = 5)
**MAX_ITEMS** = Maximum number of total video slots allowed in a layout. Any generated or cached arrangement whose total item count (top-level slots plus any nested sub-items) exceeds this value is discarded. Set to `0` to disable the limit and allow layouts of any size. (default = 0)




### ARRANGEMENTS FINGERPRINT

When the app starts, it normally computes a fingerprint of the current `installation.csv` contents and the names and sizes of every `.mp4` in the video folder. This fingerprint is saved alongside the cached arrangement binary in the `arrangements/` folder. On the next startup, if the fingerprint no longer matches — because a video was added, removed, or swapped — the stale arrangement cache is automatically deleted and new arrangements are generated from scratch.

This is the correct behavior for a live installation, but it can be disruptive during testing: cutting a single video from the folder or tweaking the CSV will wipe all cached arrangements and force a full regeneration, even if the existing layouts are still perfectly usable.

**IGNORE_FINGERPRINT** = Options: [true, false] When `true`, the fingerprint check is skipped entirely on startup. The app scans the `arrangements/` folder for any cached arrangement file that matches the current `BOX_WIDTH`, `BOX_HEIGHT`, and `NESTING_LAYERS` values. If a matching file is found it is loaded and used directly — no fingerprint is read, compared, or written. If no matching file exists, new arrangements are generated and saved as normal (still without writing a fingerprint). Set to `false` to restore normal fingerprint-based cache invalidation. (default = false)



### ARRANGEMENTS ITEM PLACEMENT

Arrangement generation uses a customized bin packing algorithm. For some general info on bin packing algorithms read here: https://en.wikipedia.org/wiki/Bin_packing_problem 
The algorithm starts by filling as much space as possible with the first video, creating a large video that typically goes the entire height of the window. It then fills in around this large item. 

Back to the two factors of arrangement video picking (area and weight), these have the most impact on the first item placement, but are used in placing every item. The next two parameters are used to control how weight vs area are prioritized. Each video is given a score based on its area and weight, and the score decides which video is placed.

The placement score is calculated using this equation:
score = (area^**PLACEMENT_AREA_EXPONENT**) * weight

From that score, the algorithm will pick one out of the top candidates for the video to be placed in the arrangement. The number of candidates it picks from is set in **PLACEMENT_TOP_K**

**PLACEMENT_AREA_EXPONENT** = This is the exponent that is used in the weighting equation, >1 favor larger area fills with less wieght (defualt 1.4) 
**PLACEMENT_TOP_K** = Number of candidates algorithm will pick from for item placement, vastly increases number of arrangments (default = 3) 
**WEIGHT_NORMALIZATION** = Controls how video counts per aspect ratio are converted into placement weights. Options: [**raw**, **sqrt**, **equal**] (default = sqrt)
- **raw**: weight equals the raw video count. A ratio with 177 videos has 44x the weight of one with 4 videos. Use this only when your video counts are already balanced across ratios, otherwise the packer will overwhelmingly favor the most common ratio and struggle to generate valid arrangements.
- **sqrt**: weight equals the square root of the video count (e.g. 177 → 13.3, 4 → 2.0). Ratios with more videos are still preferred, but the imbalance is compressed enough that all ratios meaningfully compete during placement.
- **equal**: all ratios get weight 1.0 regardless of video count. Use this to treat all aspect ratios as equally likely candidates during layout generation.



### ARRANGEMENTS EXPAND

Due to the nature of arranging videos with set aspect ratios, there are certain times where there aren't enough aspect ratios to generate any valid arrangements. One way we get around that is having expand options. Expand options allow each video to be expanded on any of its four edges to fill in empty spaces that the video would leave blank at its default aspect ratio. Videos are always drawn centered in their slot, so expanding any edge crops the video from its center outward — no stretching occurs.

For example, if a 1:1 video is placed into a 100x110px slot and `expandBottom = 0.1`, the algorithm allows the video to expand downward by up to 10% of its height (10px), filling the 10px gap at the bottom. The video is drawn as a 110x110 frame centered in the slot, with 5px cropped from the top and bottom.

Expand values are defined per **aspect ratio range** using `EXPAND_RANGE`, so portrait videos, square videos, and landscape videos can all have different tolerances. A fallback applies to any ratio not matched by a range.

**EXPAND_RANGE** = Defines directional expand allowances for a range of aspect ratios. Format:
```
EXPAND_RANGE = [minRatio, maxRatio, expandTop, expandRight, expandBottom, expandLeft]
```
- `minRatio` / `maxRatio`: the inclusive aspect ratio range (width/height, e.g. 0.667 = portrait, 1.5 = landscape)
- `expandTop` / `expandRight` / `expandBottom` / `expandLeft`: how much each edge is allowed to stretch as a fraction of the item's size (e.g. 0.1 = up to 10%)
- Multiple `EXPAND_RANGE` lines are allowed. **First match wins.** If two ranges overlap, a warning is logged.

**EXPAND_FALLBACK** = Directional expand values used for any ratio not matched by any `EXPAND_RANGE`. Format:
```
EXPAND_FALLBACK = [top, right, bottom, left]
```

**ASPECT_EXPAND_FILTER** = Boolean that controls whether arrangements violating expand tolerances are rejected. When `true`, each slot's actual pixel aspect ratio is checked independently against horizontal tolerance (`expandLeft + expandRight`) and vertical tolerance (`expandTop + expandBottom`) — both must pass. Options: [true, false] (default = true)

**GAP_FILTER_THRESHOLD** = Arrangements where the largest empty rectangle is ≥ this area (pixels²) are rejected. Set to `0` to only accept perfect fills (no empty space anywhere in the layout). Higher values allow arrangements with small residual gaps. This check runs both at generation time and when loading cached arrangements on startup. (default = 0)

**PACKING_STOP_AREA** = The bin packing algorithm stops adding items to the layout when the largest item that could still fit would be smaller than this area (pixels²). This prevents the layout from being filled with extremely tiny video slots. For example, at `40000` the algorithm stops when no item larger than roughly 200×200px can fit. (default = 40000)



### NESTING

Nesting allows a single large slot in the layout to contain its own inner sub-layout of multiple smaller videos, creating a picture-in-picture effect. Nesting uses the same bin-packing algorithm recursively and is separate from the Break Box system below.

**NESTING_LAYERS** = Number of recursive nesting layers to attempt. `0` = no nesting (each slot maps to exactly one video). `1` = after the main layout is placed, the algorithm tries to subdivide one of the larger items into its own tiled inner arrangement of multiple videos. (default = 0)

**NESTED_MIN_SPACE_THRESHOLD** = When nesting is active, this is the minimum area (pixels²) the nested packing algorithm uses as its stopping condition — analogous to `PACKING_STOP_AREA` for the inner layout. Higher values produce fewer, larger items inside the nested sub-layout. (default = 0)



### BREAK BOX

The Break Box system allows a single large slot to be replaced during layout generation by a group of smaller slots that together cover the same area. Unlike nesting, the sub-items become full members of the top-level layout rather than an inner sub-layer. Only one break is allowed per layout.

**MAIN_BIN_FILL_CHANCE** = Probability (0.0–1.0) that the very first item placed is allowed to match the full window's own aspect ratio, which would fill the entire canvas with one video. At the default `0.05` there is a 5% chance of this; otherwise the first item is forbidden from matching the window ratio, ensuring each layout always contains at least two videos. (default = 0.05)

**ITEM_BREAK_SCALE** = The minimum fraction of total canvas area an item must cover to be eligible for breaking into sub-items. For example, `0.35` means only items occupying ≥ 35% of the canvas can break. Small items placed in corners are never eligible. (default = 0.35)

**ITEM_BREAK_CHANCE** = When an item is eligible to break (meets the `ITEM_BREAK_SCALE` threshold), this is the probability (0.0–1.0) that the break actually happens. `0.5` = 50% chance. (default = 0.5)

**BREAK_BOX_MIN_ITEMS** = The minimum number of sub-items a break must produce to be accepted. The algorithm will not keep a break result that yields fewer items than this. (default = 1)

**BREAK_BOX_MAX_ITEMS** = The maximum number of sub-items a break can produce. The actual target count is chosen randomly between `BREAK_BOX_MIN_ITEMS` and `BREAK_BOX_MAX_ITEMS` for each break attempt. (default = 4)

**BREAK_BOX_FILL_ATTEMPTS** = How many times the algorithm will try to fill the break box at the current target item count before stepping down by one and trying with one fewer sub-item. If no valid arrangement is found at count N after this many tries it attempts N-1, then N-2, and so on down to `BREAK_BOX_MIN_ITEMS`. (default = 5)

**BREAK_BOX_COVERAGE_THRESHOLD** = The fraction (0.0–1.0) of the original item's area that must be collectively covered by the sub-items for the break to be accepted. `0.99` means the sub-items must fill at least 99% of the original slot's area — essentially no visible gaps allowed inside the break box. (default = 0.99)

