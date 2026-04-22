# OPEN FRAMEWORKS README FOR TAKING STOCK INSTALL
### Last edited by Tench C 04.19.26

Hello future person! This is the folder where you place the unzipped folder called *of_v.0.12.1_osx_release* available for download here:https://openframeworks.cc/download/

It is important that you place that exact folder and not just the contents or the zip file. Do not change the name of the folder.

## IMPORTANT

### in this github repo is an altered version of the file ofAVFoundationVideoPlayer.m

The default version of that file needs to be replaced for the app to work. To replace the default file, open the terminal and input the following commands:

`cd (path to takingstock_app folder)`
`cp openframeworks/ofAVFoundationVideoPlayer.m (path to takingstock_app folder)/openframeworks/of_v.0.12.1_osx_release/libs/openFrameworks/video/`

If you want to copy it without the terminal, you can copy it into *of_v.0.12.1_osx_release/libs/openFrameworks/video/*

After you're done with that command, you can go back to the README.md in takingstock_app, one directory up from this one.
