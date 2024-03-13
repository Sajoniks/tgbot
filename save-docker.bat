@echo off

echo Building image
docker build -t eldenbot . || goto error
cd docker
echo Saving image
docker save -o eldenbot.tar eldenbot || goto error
powershell Compress-Archive -Update eldenbot.tar eldenbot.tar.zip || goto error
del eldenbot.tar || goto error

goto done

:error
echo Failed with error %errorlevel%
exit /b %errorlevel%

:done
echo Done
exit