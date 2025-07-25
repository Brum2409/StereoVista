# Description
The sample integrates with 3Dconnexion's ActionInput and Navigation3D libraries to handle 3D mouse
input and commands, providing functionality for interacting with the application.

Here's a summary of the functionality:
•	Initializes the ActionInput object.
•	Subscribes to various events (ExecuteCommand, KeyDown, KeyUp, SettingsChanged).
•	Enables raising events and opens the 3D mouse with a sample name.
•	Handles commands executed by the ActionInput object.
•	Handles key down and key up events.
•	Handles settings changes.
•	Exports application commands to a dictionary.
•	Creates a command set (menu bar) with categories and commands (e.g., File, Selection, View, Help).
•	Adds the command set to the ActionInput object and sets it as the active command set.
•	Exports command images from resources and adds them to the ActionInput object.


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
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples\ActionInputCS directory.
4. Select the solution file ActionInputCS.sln and press open.
6. Select 'BUILD'->'Build Solution' or press F7.

