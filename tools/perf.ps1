# Perf matrix on the console: . tools\vita.ps1; . tools\live.ps1; . tools\perf.ps1; Perf-Run -Types 0,3 -Env "SH4_RES=960x544"
# For each world jump type: launch (new game route + SH4_WARP), wait, then summarise the [perf] lines of the last 40 s.
function Perf-Summary([string]$Log) {
    $t = Get-Content $Log
    $p = $t | Select-String "^\[perf\] ([\d.]+) ms/frame; frame body ([\d.]+) ms of which game code ([\d.]+) ms and GL ([\d.]+) ms" | Select-Object -Last 12
    if (-not $p) { return "no perf lines" }
    $m = { param($i) ($p | % { [double]$_.Matches[0].Groups[$i].Value } | Measure-Object -Average -Maximum) }
    $wall = & $m 1; $body = & $m 2; $gl = & $m 4
    $vram = ($t | Select-String "free vram (\d+) KB" | Select-Object -Last 1).Matches[0].Groups[1].Value
    $long = ($t | Select-String "frames > 36 ms: (\d+) of (\d+)" | Select-Object -Last 1).Matches[0].Groups
    $rq = ($t | Select-String "render active ([\d.]+) ms" | Select-Object -Last 1).Matches[0].Groups[1].Value
    "{0:N1} ms/frame avg (max {1:N1}) | body {2:N1} ms | GL {3:N1} | render thread {4} ms | >36ms frames {5}/{6} | free vram {7} KB" -f $wall.Average, $wall.Maximum, $body.Average, $gl.Average, $rq, $long[1].Value, $long[2].Value, $vram
}
function Perf-Run([int[]]$Types = @(0), [string]$EnvLines = "", [int]$Wait = 150, [int]$WarpFrame = 3750) {
    foreach ($ty in $Types) {
        $e = "SH4_WARP=${WarpFrame}:$ty"; if ($EnvLines) { $e += ";$EnvLines" }
        Vita-Cmd "quit all" 2500 | Out-Null
        powershell -File "$script:T\vrun.ps1" -EnvLines $e -Wait 5 -Log "$script:B\perf_launch.txt" | Out-Null
        Start-Sleep $Wait
        $l = Live-Log "$script:B\live\perf_$ty.txt"
        "type $ty [$EnvLines]: " + (Perf-Summary "$script:B\live\perf_$ty.txt")
    }
}

