CC=CL
LINK=link.exe
INCLUDEDIR=/I"include"
CFLAGS=/DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS /D_X86_ /W4 /O2 /GS /LD $(INCLUDEDIR) 
LFLAGS=/MACHINE:x86 /RELEASE kernel32.lib user32.lib gdi32.lib advapi32.lib wtsapi32.lib userenv.lib

display.exe : display.obj
	$(LINK) $(LFLAGS) display.obj

display.obj : display.c
	$(CC) $(CFLAGS) /c display.c

clean : 
	if exist display.obj del display.obj
	if exist display.exe del display.exe
