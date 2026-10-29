# Contact sheet of the SH4_DUMP captures: .\tools\sheet.ps1 [folder] [first] [last] [columns]  -> sheet.png in the folder
param([string]$dir = "J:\sh4vita\build\pc", [int]$from = 0, [int]$to = 999999, [int]$cols = 4)
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem "$dir\frame_*.bmp" | Where-Object { $n = [int]($_.BaseName -replace 'frame_', ''); $n -ge $from -and $n -le $to } | Sort-Object Name | Select-Object -First ($cols * 4)
$w = 320; $h = 240; $rows = [math]::Ceiling($files.Count / $cols)
$sheet = New-Object System.Drawing.Bitmap ($cols * $w), ($rows * $h); $g = [System.Drawing.Graphics]::FromImage($sheet); $i = 0
foreach ($f in $files) { $b = [System.Drawing.Bitmap]::new($f.FullName); $g.DrawImage($b, ($i % $cols) * $w, [int][math]::Floor($i / $cols) * $h, $w, $h); $b.Dispose(); $i++ }
$sheet.Save("$dir\sheet.png", [System.Drawing.Imaging.ImageFormat]::Png)
($files | ForEach-Object { $_.BaseName -replace 'frame_', '' }) -join ' '
