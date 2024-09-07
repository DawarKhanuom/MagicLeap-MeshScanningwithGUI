# MagicLeap-MeshScanningwithGUI


  Magic Leap Meshing Sample - Extended Version

This repository contains an extended version of the Magic Leap Meshing sample, originally based on the Magic Leap SDK's `meshing_material.h` example. Our updated version includes additional functionality for enhanced scanning, point cloud and mesh saving, and a user interface (UI) for starting and stopping scans, as well as saving mesh and point cloud data.

## Features

### New Features in This Version:
- **Enhanced GUI**: Added buttons to start and stop scanning, save meshes, save point clouds, and take screenshots.
- **Mesh and Point Cloud Saving**:
  - Save the scanned mesh in `.obj` format.
  - Save the point cloud data in `.ply` format. {not working now} 
- **Screenshot Functionality**: {tba} 
- **Mesh LOD Switching**: Using the controller’s bumper button, you can toggle between different Levels of Detail (LOD) for the mesh rendering.

### Acknowledgments:
This extended version builds upon Magic Leap's meshing sample, utilizing the core components provided in `meshing_material.h`:
```cpp
#include "meshing_material.h"
#include <app_framework/application.h>
#include <app_framework/components/magicleap_mesh_component.h>
#include <app_framework/components/renderable_component.h>
#include <app_framework/convert.h>
#include <app_framework/gui.h>
#include <ml_head_tracking.h>
#include <ml_input.h>
#include <ml_meshing2.h>
#include <ml_perception.h>
====================================================
The original sample demonstrated basic meshing capabilities and mesh visualization, which we have expanded to include mesh saving and a more user-friendly interface.

How to Build and Run
Prerequisites:
Magic Leap 2 SDK
CMake
Android NDK
Android Studio (for deployment)
Building the Project:
Clone the repository:
bash
Copy code
git clone  ??? 
For easy build, clone it to mlsdk direcotry: mlsdk\v1.7.0\samples\c_api\samples\meshingdk 
Open the project in Android Studio.
Sync Gradle and build the project.
Connect your Magic Leap device and deploy the app.
Controls:
Start Scan: Initiates the scanning process to gather mesh data.
Stop Scan: Halts the scanning process.
Save Mesh: Saves the currently scanned mesh to /files/MeshOutput.obj in .obj format.
Save PointCloud: Saves the point cloud data to /files/PointCloudOutput.ply.
Save Screenshot: Captures and saves a screenshot of the current view.
LOD Toggle: Use the bumper button on the controller to toggle through different Levels of Detail (LOD) for the mesh.
File Locations:
Meshes: /data/data/com.magicleap.capi.sample.meshing/files/MeshOutput.obj
Point Clouds: /data/data/com.magicleap.capi.sample.meshing/files/PointCloudOutput.ply
Screenshots: /data/data/com.magicleap.capi.sample.meshing/files/Screenshot.png
Future Work:
Incorporating real-time mesh streaming capabilities.
Optimization for mesh generation and point cloud accuracy.


................................................


Here us readme fir origiona # Meshing Sample App

This sample demonstrates how to setup meshing API.


## Prerequisites

Refer to https://developer-docs.magicleap.cloud/docs/guides/native/getting-started/native-getting-started

## Gui
 - None

## Running on device

```sh
adb install ./app/build/outputs/apk/ml2/debug/com.magicleap.capi.sample.meshing-debug.apk
adb shell am start -a android.intent.action.MAIN -n com.magicleap.capi.sample.meshing/android.app.NativeActivity
```

## Removing from device

```sh
adb uninstall com.magicleap.capi.sample.meshing
```

## What to Expect

 - Room around should be covered with triangles
 - The mesh should get better with each frame
 - You can change LOD by clicking Bumper button, there are 3 levels
