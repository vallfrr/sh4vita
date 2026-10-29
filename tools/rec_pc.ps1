# Video + audio capture of the PC game (borderless 1280x720 window, real time): build\rec\raw.mp4 + audio.pcm + game.log
param([int]$Seconds = 225, [string]$Route = "")
$ff = "J:\tools\ffmpeg_dl\x\ffmpeg-9.0.2-essentials_build\bin\ffmpeg.exe"
$out = "J:\sh4vita\build\rec"; New-Item -ItemType Directory -Force $out | Out-Null
Remove-Item "$out\raw.mp4","$out\audio.pcm" -ErrorAction SilentlyContinue
Remove-Item "J:\sh4vita\build\pc\game\save\sh4.sav*" -ErrorAction SilentlyContinue
$env:SH4_WIN = "1280x720"; $env:SH4_RES = "1280x720"; $env:SH4_VBO = "1"; $env:SH4_STATS = "1"
$env:SH4_WAV = "$out\audio.pcm"; $env:SDL_AUDIODRIVER = "dummy"; $env:SH4_INPUT = $Route
Set-Location J:\sh4vita\build\pc
$g = Start-Process .\sh4.exe -ArgumentList '"J:/sh4vita/build/pc/game"' -PassThru
for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 250; $g.Refresh(); if ($g.MainWindowHandle -ne 0) { break } }
Start-Sleep -Milliseconds 500
& $ff -y -f gdigrab -framerate 30 -draw_mouse 0 -use_wallclock_as_timestamps 1 -i "title=Silent Hill 4 (sh4vita)" -t $Seconds -c:v libx264 -preset ultrafast -crf 16 -pix_fmt yuv420p "$out\raw.mp4" 2> "$out\ffmpeg.log"
Stop-Process -Id $g.Id -Force -ErrorAction SilentlyContinue
Copy-Item J:\sh4vita\build\pc\sh4log.txt "$out\game.log" -Force
