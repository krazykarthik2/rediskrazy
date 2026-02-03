rem call run.bat
echo Building...
call build.bat

rem call run.bat

echo Waiting for server to initialize...
timeout /t 2 /nobreak >nul

echo Starting Client (Python Test)...
python test.py

echo ========================================
echo Run Sequence Complete
echo ========================================
