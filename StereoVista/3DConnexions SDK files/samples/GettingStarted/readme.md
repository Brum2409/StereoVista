# Description
The sample is a Windows Forms application that serves as a starting point for using the 3Dconnexion
Software Developer Kit (SDK) in a 3D viewport to control navigation using a 3Dconnexion SpaceMouse.

The key functionalities implemented are:

•	Initializes the camera and model for the 3D viewport.
•	Sets up the navigation model.
•	Implements properties and methods from the IViewModel interface.

For a step to step guided description please refer to the 'GettingStarted.pdf'.

# Building the sample
## Prerequisite
The environment variable is set to the location of the 3Dconnexion SDK i.e.
TDxWARE_SDK_DIR=D:\sdks\3dconnexion\3DxWare_SDK_v4-0-4_r21333

## Visual Studio on Windows
### Prerequisites
1. Visual Studio 2022 is installed.
2. The C# sample requires the '.NET desktop development' workload.

#### Building the samples using the Visual Studio solutions
1. Start Visual Studio.
2. Select 'Open a project or solution'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples\GettingStarted directory.
4. Select the solution file GettingStated.sln and press open.
6. Select 'BUILD'->'Build Solution' or press F7.
