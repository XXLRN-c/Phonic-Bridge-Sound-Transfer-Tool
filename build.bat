@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
rc.exe version.rc
cl main.cpp version.res /EHsc /Fe:app.exe
