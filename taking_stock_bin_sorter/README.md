# Bin Sorter - OpenFrameworks Video Layout

A port of the Python BinSorter that arranges videos in a bin-packing layout. Videos are loaded from aspect-ratio-named folders (e.g. `1_1`, `2_3`).

## Setup

### 1. Create project with OpenFrameworks Project Generator

1. Open the OpenFrameworks **Project Generator**
2. Set the **Path** to your OF root (e.g. `~/Documents/openFrameworks`)
3. Set **Project Name** to `bin_sorter`
4. Set **Project Path** to `apps/myApps` (or your preferred location)
5. Click **Generate**

### 2. Replace generated source with this implementation

Replace the contents of the generated `src/` folder with the files from this `src/` folder:

- main.cpp
- ofApp.cpp, ofApp.h
- BinSorter.cpp, BinSorter.h
- VideoAssetPool.cpp, VideoAssetPool.h
- BinSorterRenderer.cpp, BinSorterRenderer.h
- ConfigLoader.cpp, ConfigLoader.h

### 3. Add data files

Copy `bin/data/config.txt` to your project's `bin/data/` folder.

Create the video asset structure under `bin/data/videos/`:

```
bin/data/videos/
├── 1_1/      # Square videos (mp4, mov, etc.)
│   ├── clip1.mp4
│   └── ...
├── 1_2/      # Portrait 1:2
├── 2_3/      # Portrait 2:3
├── 3_2/      # Landscape 3:2
├── 1_3/
├── 3_1/
└── ...
```

Folder names must match the `SIZE_RATIO` entries in `config.txt`.

### 4. Build and run

- **macOS**: Open the Xcode project, build and run
- **Linux**: `make` then `make run`
- **Windows**: Open the Visual Studio solution, build and run

## Controls

- **s**: Export current frame to `bin_sorter_export.png`
- **r**: Regenerate layout (new random arrangement)

## Config

Edit `bin/data/config.txt` to change:

- `BOX_WIDTH`, `BOX_HEIGHT`: Output dimensions
- `VIDEO_ASSET_PATH`: Path to videos (relative to bin/data)
- `SIZE_RATIO`: One line per aspect ratio (`wr hr weight expandX expandY`)
- `NESTING_LAYERS`, `ITEM_BREAK_*`, `BREAK_BOX_*`: Algorithm parameters
