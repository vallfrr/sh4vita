# Lab: reproduce the window interaction ("What's with this room?") and report the freeze stats.
# Usage: . tools\vita.ps1; . tools\live.ps1; . tools\lab.ps1; Lab-Start; Lab-Window "rtsync=1"
function Lab-Start([int]$MinPresents = 4300) {
    Live-Keys "" ""
    Vita-Cmd "quit all" 1500 | Out-Null
    powershell -File "$script:T\vrun.ps1" -Wait 20 -Route "800:01:3,1300:1c:3,1500:1c:3,1700:1c:3,3200:1c:3,3400:1c:3" -Log "$script:B\lab_launch.txt"
    for ($i = 0; $i -lt 40; $i++) {   # wait for the room (intro text + load)
        Start-Sleep 5
        $t = (curl.exe -s --max-time 20 "$script:F/sh4log.txt") -join "`n"
        $m = [regex]::Matches($t, "presents=(\d+)")
        if ($m.Count -and [int]$m[$m.Count - 1].Groups[1].Value -ge $MinPresents) { break }
    }
    Start-Sleep 5
}
function Lab-Window([string]$Extra = "") {
    foreach ($i in 1..3) { Live-Tap "20" 560 }; Live-Tap "20" 90; Live-Tap "11" 1200
    Live-Keys "39" $Extra; Start-Sleep 1.3; Live-Keys "" $Extra; Start-Sleep 8
    $l = Live-Log
    Select-String -Path $l -Pattern "\[frz\]|CreateTexture RT" | Select-Object -Last 8 | ForEach-Object { $_.Line }
    Live-Shot "lab_$([guid]::NewGuid().ToString().Substring(0,4))"
    Live-Keys "" ""
}
