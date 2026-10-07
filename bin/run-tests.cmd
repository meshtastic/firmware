@echo off
rem Forwarder, not a port. bin\run-tests.sh is Linux-only by design (its HOST note says why); this
rem runs it inside WSL from this same checkout, passing the arguments and the exit code straight
rem through, so the same command line and the same RESULT: line work from cmd.exe and PowerShell.
rem
rem   bin\run-tests.cmd -e native -f test_utf8 --quiet
rem   bin\run-tests.cmd --status
rem
rem Needs WSL with a distro that has the PlatformIO venv. A checkout on the Windows drive (/mnt/c)
rem builds several times slower than one on the WSL filesystem (\\wsl$\...); both paths work here.
wsl.exe --cd "%~dp0.." -e ./bin/run-tests.sh %*
exit /b %ERRORLEVEL%
