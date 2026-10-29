# Live play helpers (Vita game already running): . tools\live.ps1 ; Live-Keys "11,20" ; Live-Shot "name" ; Live-Log
# Keys are DirectInput scancodes in hex (11 W, 1f S, 1e A, 20 D, 1c Enter, 01 Esc, c8/d0 up/down...) held while present in ctl.txt (re-read every 30 frames).
$script:F = "ftp://192.168.1.175:1337/ux0:/data/sh4"
$script:T = $PSScriptRoot
$script:B = "J:\sh4vita\build"
function Live-Put([string]$Local, [string]$Name) {   # upload under a temp name then rename: the game polls these files and must never see a partial one
    curl.exe -s --max-time 20 -T $Local "$script:F/$Name.tmp"
    curl.exe -s --max-time 20 -Q "-DELE ux0:/data/sh4/$Name" "$script:F/" -o NUL
    curl.exe -s --max-time 20 -Q "-RNFR ux0:/data/sh4/$Name.tmp" -Q "-RNTO ux0:/data/sh4/$Name" "$script:F/" -o NUL
}
function Live-Keys([string]$Keys = "", [string]$Extra = "") {
    $line = "0 $Extra" + $(if ($Keys) { " keys=$Keys" } else { "" })
    [IO.File]::WriteAllText("$script:B\ctl.txt", "$line`n")
    Live-Put "$script:B\ctl.txt" "ctl.txt"
}
function Live-Press([string]$Keys, [double]$Sec = 0.6, [string]$Extra = "") {
    Live-Keys $Keys $Extra; Start-Sleep -Milliseconds ([int]($Sec * 1000) + 900); Live-Keys "" $Extra; Start-Sleep -Milliseconds 900
}
function Live-Shot([string]$Name = "live", [int]$Tries = 5) {   # captures until the lower half is not empty (a capture is sometimes half-rendered)
    for ($t = 0; $t -lt $Tries; $t++) {
        # Frame numbers restart at each launch: an old run's shot can have a larger
        # number than the new capture. Only accept files created by this request.
        $before = @((curl.exe -s --max-time 20 -l "$script:F/") -split "`r?`n" | Where-Object { $_ -match "^shot_\d+\.tga$" })
        [IO.File]::WriteAllText("$script:B\shotnow", "x"); Live-Put "$script:B\shotnow" "shotnow"
        Start-Sleep 5
        $n = (curl.exe -s --max-time 20 -l "$script:F/") -split "`r?`n" | Where-Object { $_ -match "^shot_(\d+)\.tga$" -and $_ -notin $before } | ForEach-Object { [int]($_ -replace "shot_|\.tga", "") } | Sort-Object | Select-Object -Last 1
        if (-not $n) { continue }
        for ($k = 0; $k -lt 15; $k++) { curl.exe -s --max-time 30 "$script:F/shot_$n.tga" -o "$script:B\shot_$n.tga"; if ((Get-Item "$script:B\shot_$n.tga").Length -ge 2088978) { break }; Start-Sleep 1 }
        $b = [IO.File]::ReadAllBytes("$script:B\shot_$n.tga"); $vals = @{}
        for ($y = 300; $y -lt 540; $y += 6) { for ($x = 0; $x -lt 960; $x += 20) { $vals[[int]$b[18 + 4 * ($y * 960 + $x) + 1]] = 1 } }
        $lit = $vals.Count
        if ($lit -gt 12) { break }
    }
    if (-not $n) { return "no shot" }
    powershell -File "$script:T\shot2png.ps1" $n
    Copy-Item "$script:B\shot_$n.png" "$script:B\live\$Name.png" -Force
    foreach ($f in ((curl.exe -s --max-time 20 -l "$script:F/") -split "`r?`n" | Where-Object { $_ -match "^shot_\d+\.tga$" })) { curl.exe -s -Q "-DELE ux0:/data/sh4/$f" "$script:F/" -o NUL }
    "$script:B\live\$Name.png ($n, lit $lit, try $t)"
}
function Live-Log([string]$Out = "J:\sh4vita\build\live\log.txt") { curl.exe -s --max-time 60 "$script:F/sh4log.txt" -o $Out; $Out }
function Live-Clean {   # old diagnostic dumps on the Vita
    foreach ($f in ((curl.exe -s --max-time 20 -l "$script:F/") -split "`r?`n" | Where-Object { $_ -match "^(shot_\d+|auto_\d+|frz_\d+_\w+)\.tga$" })) { curl.exe -s -Q "-DELE ux0:/data/sh4/$f" "$script:F/" -o NUL }
}
function Live-Tap([string]$Key, [int]$Ms = 300) {   # timed key pulse (data/sh4/key.txt in ms, consumed by the game)
    [IO.File]::WriteAllText("$script:B\key.txt", "$Key $Ms"); Live-Put "$script:B\key.txt" "key.txt"
    Start-Sleep -Milliseconds ($Ms + 1500)
}
