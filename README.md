# patch-scanner

GCC & G++ needed parameters:

Console version:  -lntdll -lshlwapi (undef "GUI" macro in main.c)\
GUI version:      -lntdll -lshlwapi -lcomctl32 -mwindows
