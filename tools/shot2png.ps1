# Usage: shot2png.ps1 1400 1800 ...  -> downloads shot_N.tga from the Vita and writes build\shot_N.png
Add-Type -AssemblyName System.Drawing
foreach ($n in $args) {
    curl.exe -s -o "J:\sh4vita\build\shot_$n.tga" "ftp://$($env:VITA_IP):1337/ux0:/data/sh4/shot_$n.tga"
    $b = [IO.File]::ReadAllBytes("J:\sh4vita\build\shot_$n.tga")
    $bmp = New-Object Drawing.Bitmap 960, 544, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $d = $bmp.LockBits((New-Object Drawing.Rectangle 0, 0, 960, 544), 'WriteOnly', 'Format32bppArgb')
    $px = New-Object byte[] (960 * 544 * 4); [Array]::Copy($b, 18, $px, 0, $px.Length)
    for ($i = 3; $i -lt $px.Length; $i += 4) { $px[$i] = 255 }
    [Runtime.InteropServices.Marshal]::Copy($px, 0, $d.Scan0, $px.Length); $bmp.UnlockBits($d)
    $bmp.Save("J:\sh4vita\build\shot_$n.png"); $bmp.Dispose()
}
