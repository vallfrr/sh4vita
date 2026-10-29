# Launches the game on the Vita with a given env.txt, waits for the log, fetches the captures.
# Usage : tools\vrun.ps1 -EnvLines "SH4_SWDXT=1","SH4_SHOT=3500" -Shots 3500 -Wait 200 -Log build\vrun.txt
param([string[]]$EnvLines = @(), [string]$ShotsArg = "", [int]$Wait = 200, [string]$Log = "J:\sh4vita\build\vrun.txt",
      [switch]$KeepSave, [string]$Route = "800:01:3,1300:1c:3,1500:1c:3,1700:1c:3,3200:1c:3,3400:1c:3,3600:11:150,3900:20:60,4100:11:200,4500:1e:60,4600:11:300,5000:20:40,5100:11:300,5500:1e:40,5600:11:300")
. J:\sh4vita\tools\vita.ps1
$Shots = @($ShotsArg -split "[, ]" | Where-Object { $_ } | ForEach-Object { [int]$_ })   # -File: "4760,4800" does not bind to int[]
$ftp = "ftp://$($env:VITA_IP):1337/ux0:/data/sh4"
Vita-Cmd "quit all" 1500 | Out-Null
$p = "J:\sh4vita\build\env.txt"
$lines = @("SH4_STATS=1") + ($EnvLines -split ";")   # -File does not pass an array: "A=1;B=2"
if ($Shots.Count) { $lines += "SH4_SHOT=" + ($Shots -join ",") }
if ($Route) { $lines += "SH4_INPUT=$Route" }
[IO.File]::WriteAllText($p, (($lines -join "`n") + "`n"))
curl.exe -s -T $p "$ftp/env.txt"
curl.exe -s -Q "-DELE ux0:/data/sh4/sh4log.txt" "$ftp/" -o NUL
foreach ($s in $Shots) { curl.exe -s -Q "-DELE ux0:/data/sh4/shot_$s.tga" "$ftp/" -o NUL }
# the save (options only) leaves the title cursor on Load: we set it aside (reversible)
if (-not $KeepSave) {
curl.exe -s -Q "-DELE ux0:/data/sh4/save/sh4.sav.prev" "$ftp/" -o NUL   # only one aside copy (RNTO fails if the target exists)
curl.exe -s -Q "-RNFR ux0:/data/sh4/save/sh4.sav" -Q "-RNTO ux0:/data/sh4/save/sh4.sav.prev" "$ftp/" -o NUL
}
Vita-Cmd "launch SILH00004" | Out-Null
$t = ""
for ($i = 0; $i -lt $Wait / 20; $i++) {
    Start-Sleep 20
    $t = (curl.exe -s "$ftp/sh4log.txt") -join "`n"
    if ($Shots.Count -and ($t -match "shot_$($Shots[-1])")) { break }
}
[IO.File]::WriteAllText($Log, $t)
if ($Shots.Count) { powershell -File J:\sh4vita\tools\shot2png.ps1 @($Shots) }
