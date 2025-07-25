# Description
The sample demonstrates how to handle input from a 3Dconnexion device, such as a SpaceMouse,
and execute various commands within an application. 
Here's a high-level overview of the CApplication class and its functionality:
•	The CApplication constructor sets up event handlers for command execution and key events
    (key down and key up).
•	The class defines several command IDs (e.g., ID_OPEN, ID_CLOSE, ID_SAVE, ID_EXIT, ID_ABOUT, etc.)
    that represent different actions the application can perform.
•	The ExportApplicationCommands method maps these command IDs to their corresponding actions
    (e.g., opening a file, saving a file, exiting the application, etc.) and exports them to the
     3Dconnexion Settings Configuration Utility.    
•	ExecuteCommandHandler: Handles the execution of commands when triggered by the 3Dconnexion device.
•	KeyDownHandler and KeyUpHandler: Handle key down and key up events from the 3Dconnexion device.

# Building the sample
## Prerequisite
The environment variable is set to the location of the 3Dconnexion SDK i.e.
TDxWARE_SDK_DIR=D:\sdks\3dconnexion\3DxWare_SDK_v4-0-3_r21333

## MSYS2 MinGW on Windows
1. Open the MSYS2 MinGW x64 terminal
2. $ cd $TDxWARE_SDK_DIR/samples/ActionInput
3. $ mkdir -p build/debug
4. $ cd build/debug
5. $ cmake ../.. -G"MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug
6. $ cmake --build .
7. You may need to copy libstdc++-6.dll to the $TDxWARE_SDK_DIR/build/debug/ActionInput directory to run the sample.

## CMake & Ninja on Windows
1. Open a command prompt
2. >cd %TDxWARE_SDK_DIR%\samples\ActionInput
3. >md out\debug
4  >cd out\debug
5. >cmake ../.. -G"Ninja" -DCMAKE_BUILD_TYPE=Debug
6. >cmake --build .

## Visual Studio on Windows
### Prerequisites
1. Visual Studio 2019 or Visual Studio 2022 is installed.
3. The C++ samples require the 'Desktop development with C++' workload.
4. The 'CMake tools for Windows' component is required to build using CMake in Visual Studio.

#### Building the samples using the Visual Studio solutions
1. Start Visual Studio.
2. Select 'Open a project or solution'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples\ActionInput directory.
4. Select the solution file ActionInput.sln and press open.
6. Select 'BUILD'->'Build Solution' or press F7.

#### Building the c++ samples using the Visual Studio CMake component
1. Start Visual Studio.
2. Select 'Open a local folder'.
3. Navigate to the SDK samples %TDxWARE_SDK_DIR%\samples directory.
4. Click on the C++ sample folder to build i.e. ActionInput and press 'Select Folder'.
5. Visual Studio opens the 'CMake Overview Pages' window.
6. Select 'Project'->'Configure Cache'.
7. The 'Output' window displays CMake messages and ends with 'CMake generation finished'.
8. If the 'Select Startup Item' dropdown box displays 'ActionInput.sln' select 'ActionInput.exe'
   or in the 'Solution Explorer' right click the folder and 'Switch to CMake Targets View (Ctrl +)'
   and then right click 'ActionInput (executable)' -> 'Set as Startup Item'. 
9. Select 'BUILD'->'Build All' or press F7.
10. The 'Output' window displays 'Build All succeeded'.

####  Alternative for building the c++ samples using the MSVC compiler & CMake
1. Open a 'Developer Command Prompt for VS 2019' or 'Developer Command Prompt for VS 2022'.
2. >cd %TDxWARE_SDK_DIR%\samples\ActionInput
3. >md out\debug
4. >cd out\debug
5. >cmake ../.. -G"Ninja" -DCMAKE_BUILD_TYPE=Debug
6. >cmake --build .
