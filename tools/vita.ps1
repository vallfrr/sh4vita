# Drives the Vita through VitaCompanion (TCP commands 1338) and its FTP (1337).
# Usage :  . .\tools\vita.ps1 ; Vita-Run -Seconds 40
param()
$script:Ip = if ($env:VITA_IP) { $env:VITA_IP } else { throw "set VITA_IP to the Vita IP address" }
$script:TitleId = "SILH00004"
$script:LogUrl = "ftp://$($script:Ip):1337/ux0:/data/sh4/sh4log.txt"

function Vita-Cmd([string]$Command, [int]$WaitMs = 1200) {
    $cl = New-Object Net.Sockets.TcpClient($script:Ip, 1338)
    $s = $cl.GetStream()
    $b = [Text.Encoding]::ASCII.GetBytes("$Command`n")
    $s.Write($b, 0, $b.Length)
    Start-Sleep -Milliseconds $WaitMs
    $buf = New-Object byte[] 4096
    $n = if ($s.DataAvailable) { $s.Read($buf, 0, 4096) } else { 0 }
    $cl.Close()
    [Text.Encoding]::ASCII.GetString($buf, 0, $n).Trim()
}

function Vita-PushEboot([string]$Eboot = "") {
    if (-not $Eboot) {
        wsl cp ~/build/sh4vita/eboot.bin /mnt/j/sh4vita/build/eboot_push.bin
        $Eboot = "J:\sh4vita\build\eboot_push.bin"
    }
    curl.exe -s --max-time 120 -T $Eboot "ftp://$($script:Ip):1337/ux0:/app/$($script:TitleId)/eboot.bin"
    "eboot sent (exit $LASTEXITCODE)"
}

function Vita-Log([string]$Out = "J:\sh4vita\build\vita_log.txt") {
    curl.exe -s --max-time 30 $script:LogUrl -o $Out
    if (Test-Path $Out) { Get-Content $Out }
}

function Vita-Run([int]$Seconds = 40) {
    # Close ALL apps first: otherwise the Vita asks on screen to close the current app.
    $null = Vita-Cmd "quit all" 1500
    curl.exe -s --max-time 15 -Q "-DELE ux0:/data/sh4/sh4log.txt" "ftp://$($script:Ip):1337/" -o NUL 2>$null
    "launch -> " + (Vita-Cmd "launch $($script:TitleId)")
    Start-Sleep -Seconds $Seconds
    $log = Vita-Log
    $null = Vita-Cmd "quit all" 1500
    $log
}
