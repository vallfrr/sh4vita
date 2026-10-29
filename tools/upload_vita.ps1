# Uploads the sh4/ folder prepared by sh4prep to the Vita (VitaShell FTP). Resumable: files
# already present at the right size are skipped.
# Usage: .\tools\upload_vita.ps1 -Ip <vita-ip> [-Src J:\sh4vita\build\vita_data\sh4] [-Dest ux0:/data/sh4]
param([Parameter(Mandatory)][string]$Ip, [string]$Src = "J:\sh4vita\build\vita_data\sh4", [string]$Dest = "ux0:/data/sh4", [int]$Port = 1337)
$root = (Resolve-Path $Src).Path
$files = Get-ChildItem $root -Recurse -File | Sort-Object FullName
$total = ($files | Measure-Object Length -Sum).Sum
$done = 0; $skipped = 0; $failed = 0; $t0 = Get-Date
"{0} files, {1:N2} GB" -f $files.Count, ($total / 1GB)
foreach ($f in $files) {
    $rel = $f.FullName.Substring($root.Length + 1).Replace('\', '/')
    $url = "ftp://${Ip}:$Port/$Dest/" + ([uri]::EscapeUriString($rel))
    $remote = curl.exe -s -I --max-time 20 $url 2>$null | Select-String -Pattern 'Content-Length: (\d+)' | ForEach-Object { [int64]$_.Matches[0].Groups[1].Value }
    if ($remote -eq $f.Length) { $skipped++; $done += $f.Length; continue }
    curl.exe -s --max-time 900 --ftp-create-dirs -T $f.FullName $url
    if ($LASTEXITCODE -ne 0) {
        $failed++
        "FAILED ($LASTEXITCODE) : $rel"
        if ($failed -ge 3) { "3 failures: stopping (card full or Vita unreachable?)"; exit 1 }
        continue
    }
    $done += $f.Length
    $el = ((Get-Date) - $t0).TotalSeconds
    "{0:N1} %  {1}  ({2:N1} MB/s avg)" -f (100 * $done / $total), $rel, ($done / 1MB / [Math]::Max($el, 1))
}
"done: $skipped skipped, $failed failed"
