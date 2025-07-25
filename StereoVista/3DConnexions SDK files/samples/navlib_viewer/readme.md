# Description
The sample is a 3D CAD viewer that allows users to view and interact with 3D models.
It uses OpenGL for rendering and MFC for the user interface and event handling. 
The CMCADView class is the core component responsible for rendering the scene, managing
the camera, and handling user interactions. The program supports different projection modes
and object picking.

The CMainFrame class which derives from TDx::SpaceMouse::Navigation3D::CNavigation3D is a
central component of the 3D CAD viewer application, managing the main application window,
integrating with the SpaceMouse for 3D navigation, handling user commands, and providing
accessors and mutators for view-related properties.

# Building the sample
## Prerequisite
The environment variable is set to the location of the 3Dconnexion SDK i.e.
TDxWARE_SDK_DIR=D:\sdks\3dconnexion\3DxWare_SDK_v4-0-4_r21333

## Visual Studio on Windows
### Prerequisites
1. Visual Studio 2022 is installed.
3. The C++ sample requires the 'Desktop development with C++' workload,
   the 'C++ ATL & MFC for for latest build tools' component and the Windows 10 SDK.
4. The 'CMake tools for Windows' component is required to build using CMake in Visual Studio.

#### Building the samples using the Visual Studio solutions
1. Start Visual Studio.
2. Select 'Open a project or solution'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples\navlib_viewer directory.
4. Select the solution file navlib_viewer.sln and press open.
6. Select 'BUILD'->'Build Solution' or press F7.

#### Building the c++ samples using the Visual Studio CMake component
1. Start Visual Studio.
2. Select 'Open a local folder'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples directory.
4. Click on the C++ sample folder to build i.e. navlib_viewer and press 'Select Folder'.
5. Visual Studio opens the 'CMake Overview Pages' window.
6. Select 'Project'->'Configure Cache'.
7. The 'Output' window displays CMake messages and ends with 'CMake generation finished'.
8. If the 'Select Startup Item' dropdown box displays 'navlib_viewer.sln' select 'navlib_viewer.exe'
   or in the 'Solution Explorer' right click the folder and 'Switch to CMake Targets View (Ctrl +)'
   and then right click 'navlib_viewer (executable)' -> 'Set as Startup Item'. 
9. Select 'BUILD'->'Build All' or press F7.
10. The 'Output' window displays 'Build All succeeded'.

####  Alternative for building the c++ samples using the MSVC compiler & CMake
1. Open a 'Developer Command Prompt for VS 2019' or 'Developer Command Prompt for VS 2022'.
2. >cd %TDxWARE_SDK_DIR%\samples\navlib_viewer
3. >md out\debug
4. >cd out\debug
5. >cmake ../.. -G"Ninja" -DCMAKE_BUILD_TYPE=Debug
6. >cmake --build .
