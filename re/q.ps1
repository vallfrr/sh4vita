# Calls an REA tool through the bridge: .\re\q.ps1 search_strings @{query='data'}
param([Parameter(Mandatory)][string]$Tool, [hashtable]$Arguments = @{}, [string]$Out)
$body = @{ name = $Tool; arguments = $Arguments } | ConvertTo-Json -Depth 20 -Compress
$r = Invoke-RestMethod -Method Post -Uri http://127.0.0.1:47800/call -Body $body -ContentType 'application/json' -TimeoutSec 0
if ($r.error) { throw ($r.error | ConvertTo-Json -Depth 10) }
$res = $r.result
$json = if ($res.structuredContent) { $res.structuredContent } else { $res.content[0].text | ConvertFrom-Json }
if ($Out) { $json | ConvertTo-Json -Depth 50 | Set-Content -Encoding utf8 $Out }
$json
