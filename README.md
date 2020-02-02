# patch-scanner

MSYS2 Compilation:

* (x86)
  - set path=C:\msys32\mingw32\bin\ 
    - Console:
      - gcc main.c            -o patch-scanner-i686.exe       -DMSYS2         -lntdll -lshlwapi -mwin32 -mconsole -static      
    - Gui:     
      - windres resource.rc resource.o
      - gcc main.c resource.o -o patch-scanner-i686_GUI.exe   -DMSYS2 -DGUI   -lntdll -lshlwapi -lcomctl32 -mwin32 -mwindows -static
* (x64)
  - set path=C:\msys32\mingw64\bin\ 
    - Console:
      - gcc main.c            -o patch-scanner-x86_64.exe     -DMSYS2         -lntdll -lshlwapi -mwin32 -mconsole -static      
    - Gui:     
      - windres resource.rc resource.o
      - gcc main.c resource.o -o patch-scanner-x86_64_GUI.exe -DMSYS2 -DGUI   -lntdll -lshlwapi -lcomctl32 -mwin32 -mwindows -static
