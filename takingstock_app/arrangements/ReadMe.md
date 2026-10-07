# ARRANGEMENT FILES FOR TAKING STOCK INSTALL
### Last edited by Tench C 10.02.26

Hello future person! This folder will hold all of the generated arrangements for each run of the taking stock install app. If you want to regenerate arrangements you can simply delete an arrangement file. 

The files are formatted as such:

aspect ratio(w/h)_s(generation settings hash)_arrangements_(number of arrangements)
ie 1.333_s3fa9c2d1_arrangements_393

The settings hash is built from the generation settings in the selected window config (set by WINDOW_CONFIG in config/system_config.txt) (PACKING_STOP_AREA, ITEM_BREAK_SCALE, ITEM_BREAK_CHANCE, BREAK_BOX_MIN_ITEMS, BREAK_BOX_MAX_ITEMS, BREAK_BOX_FILL_ATTEMPTS, PLACEMENT_AREA_EXPONENT, PLACEMENT_TOP_K, WEIGHT_NORMALIZATION, LAYOUT_MAX_ATTEMPTS, LAYOUT_STALE_THRESHOLD, LAYOUT_PHASES) and from RATIO_ROUND_DECIMALS in system_config.txt. On startup the app only reuses a file whose aspect ratio and settings hash both match the current config; if any of those settings change, new arrangements are generated automatically. The current hash is printed in the startup log.

When a new set of arrangements is saved, every other arrangement file for the same aspect ratio is deleted (including files made with different settings and older files named like 1.333_nest0_arrangements_393). Files for other aspect ratios are left alone.

You may also see a file named like 1.333_arrangements_inputs.fingerprint. This is only used when IGNORE_FINGERPRINT = false: it records the state of installation.csv and the video files so arrangements are regenerated when the videos change. It is never deleted by the cleanup above.

An explanation of what arrangements are can be found in the readme one directory up from this one, and details on the arrangement cache are in config/configREADME.md under ARRANGEMENTS CACHE.
