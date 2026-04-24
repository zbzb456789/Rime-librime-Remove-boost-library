rem Customize your build environment and save the modified copy to env.bat

set RIME_ROOT=%CD%


rem architecture, Visual Studio version and platform toolset
set ARCH=x64
set BJAM_TOOLSET=msvc-14.5
set CMAKE_GENERATOR="Visual Studio 18 2026"
set PLATFORM_TOOLSET=v145

rem OPTIONAL: path to additional build tools
rem set DEVTOOLS_PATH=%ProgramFiles%\Git\cmd;%ProgramFiles%\CMake\bin;C:\Python27;
