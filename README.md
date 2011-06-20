# RotateDisplay
rotate display when DSR of COM4 becomes signaled state.

# install as a service
    C:\>sc create RotateDisplay binpath= foo\bar\display.exe displayname= RotateDisplay
	C:\>net start RotateDisplay

# uninstall
	C:\>net stop RotateDisplay
	C:\>sc delete RotateDisplay


