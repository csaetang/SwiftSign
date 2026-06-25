@echo off
title SwiftSign Gesture Recognition

echo ==================================
echo        Starting SwiftSign
echo ==================================
echo.

cd /d "%~dp0"

python Python\gesture_recognition.py

echo.
echo SwiftSign has stopped.
pause