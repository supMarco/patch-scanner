# patch-scanner

MSYS2 Compilation:

set path=C:\msys32\mingw32\bin\
gcc main.c -o patch-scanner-i686.exe       -DMSYS2         -lntdll -lshlwapi -lcomctl32 -mwin32 -mconsole -static\
gcc main.c -o patch-scanner-i686_GUI.exe   -DMSYS2 -DGUI   -lntdll -lshlwapi -lcomctl32 -mwin32 -mwindows -static\

set path=C:\msys32\mingw64\bin\
gcc main.c -o patch-scanner-x86_64.exe     -DMSYS2         -lntdll -lshlwapi -lcomctl32 -mwin32 -mconsole -static\
gcc main.c -o patch-scanner-x86_64_GUI.exe -DMSYS2 -DGUI   -lntdll -lshlwapi -lcomctl32 -mwin32 -mwindows -static
