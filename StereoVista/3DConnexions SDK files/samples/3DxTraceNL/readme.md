# Description
This is a simple C++ sample that does not depend on any external references.
The console application displays traces of the communication between the navlib library
and the navigation model implementation.

The CApplication3D class in the 3DxTraceNL program emulates a virtual 3D application and 
provides several key features and functionalities:
•	The constructor initializes the navigation model and connects various event handlers
    (ExecuteCommandHandler, KeyDownHandler, KeyUpHandler, SettingsChangedHandler).
•	The Enable3DNavigation method initializes the navigation model and enables input
    from the Navigation3D controller - the SpaceMouse.
•	The Disable3DNavigation method disables the 3D navigation.    
•	The ExportApplicationCommands method exports application commands to the 3Dconnexion
    Settings Configuration Utility.
•	The ExportCommandImages method exports images for the commands to the 3Dconnexion
    Settings Configuration Utility.    

# Building the sample
## Prerequisite
The environment variable is set to the location of the 3Dconnexion SDK i.e.
TDxWARE_SDK_DIR=D:\sdks\3dconnexion\3DxWare_SDK_v4-0-4_r21333

## MSYS2 MinGW on Windows
1. Open the MSYS2 MinGW x64 terminal
2. $ cd $TDxWARE_SDK_DIR/samples/3DxTraceNL
3. $ mkdir -p build/debug
4. $ cd build/debug
5. $ cmake ../.. -G"MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug
6. $ cmake --build .
7. You may need to copy libstdc++-6.dll to the $TDxWARE_SDK_DIR/build/debug/3DxTraceNL directory to run the sample.

## CMake & Ninja on Windows
1. Open a command prompt
2. >cd %TDxWARE_SDK_DIR%\samples\3DxTraceNL
3. >md out\debug
4  >cd out\debug
5. >cmake ../.. -G"Ninja" -DCMAKE_BUILD_TYPE=Debug
6. >cmake --build .

## Visual Studio on Windows
### Prerequisites
1. Visual Studio 2022 is installed.
3. The C++ samples require the 'Desktop development with C++' workload.
4. The 'CMake tools for Windows' component is required to build using CMake in Visual Studio.

#### Building the samples using the Visual Studio solutions
1. Start Visual Studio.
2. Select 'Open a project or solution'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples\3DxTraceNL directory.
4. Select the solution file 3DxTraceNL.sln and press open.
6. Select 'BUILD'->'Build Solution' or press F7.

#### Building the c++ samples using the Visual Studio CMake component
1. Start Visual Studio.
2. Select 'Open a local folder'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples directory.
4. Click on the C++ sample folder to build i.e. 3DxTraceNL and press 'Select Folder'.
5. Visual Studio opens the 'CMake Overview Pages' window.
6. Select 'Project'->'Configure Cache'.
7. The 'Output' window displays CMake messages and ends with 'CMake generation finished'.
8. If the 'Select Startup Item' dropdown box displays '3DxTraceNL.sln' select '3DxTraceNL.exe'
   or in the 'Solution Explorer' right click the folder and 'Switch to CMake Targets View (Ctrl +)'
   and then right click '3DxTraceNL (executable)' -> 'Set as Startup Item'. 
9. Select 'BUILD'->'Build All' or press F7.
10. The 'Output' window displays 'Build All succeeded'.

####  Alternative for building the c++ samples using the MSVC compiler & CMake
1. Open a 'Developer Command Prompt for VS 2019' or 'Developer Command Prompt for VS 2022'.
2. >cd %TDxWARE_SDK_DIR%\samples\3DxTraceNL
3. >md out\debug
4. >cd out\debug
5. >cmake ../.. -G"Ninja" -DCMAKE_BUILD_TYPE=Debug
6. >cmake --build .
