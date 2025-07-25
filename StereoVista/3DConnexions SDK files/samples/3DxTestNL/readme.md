# Description
The sample is a WPF application that integrates with 3Dconnexion's SpaceMouse for 3D navigation.
Here's a summary of its functionality:

1.	Initialization: The MainExecutor class is instantiated when the application starts.
   It initializes the main window's view model and sets up event handlers for various
   ribbon buttons.
2.	Event Handlers: It includes handlers for opening and closing files, changing projection
   modes (parallel and perspective), selecting all items, clearing selections, and exiting
   the application.
3.	SpaceMouse Integration: The program initializes a NavigationModel for the SpaceMouse,
   enabling 3D navigation. It also exports application commands to the 3Dconnexion Settings
   Utility.
4.	File Operations: The OpenFileHandler method allows users to open 3D model files using an
   OpenFileDialog. The models are read using an ObjReader, and the loading progress is tracked
   with a callback method.
5.	UI Updates: The program updates the UI based on user interactions and model loading status,
   ensuring a responsive experience.
6.	About Dialog: The AboutHandler method displays information about the application, including
   version and copyright details.

This program is designed to demonstrate the seamless 3D navigation and model viewing experience
using the SpaceMouse, with a focus on user interaction through a ribbon interface.


# Building the sample
## Prerequisite
The environment variable is set to the location of the 3Dconnexion SDK i.e.
TDxWARE_SDK_DIR=D:\sdks\3dconnexion\3DxWare_SDK_v4-0-4_r21333

## Visual Studio on Windows
### Prerequisites
1. Visual Studio 2022 is installed.
2. The C++ sample requires the '.NET desktop development' workload

#### Building the samples using the Visual Studio solutions
1. Start Visual Studio.
2. Select 'Open a project or solution'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples\3DxTestNL directory.
4. Select the solution file 3DxTestNL.sln and press open.
6. Select 'BUILD'->'Build Solution' or press F7.

