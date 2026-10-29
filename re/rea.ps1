# Launches REA with Ghidra/JDK configured and the default SH4 snapshot.
# Ex : .\re\rea.ps1 decompile 0x401000
param([Parameter(Mandatory)][string]$Cmd, [Parameter(ValueFromRemainingArguments)][string[]]$Rest)
$env:GHIDRA_INSTALL_DIR = "J:\tools\ghidra_12.1.4_PUBLIC"
$env:JAVA_HOME = "J:\tools\jdk-21.0.12.1+1"
$env:GHIDRA_HEADLESS_MAXMEM = "8G"
$env:NO_COLOR = "1"
rea $Cmd J:\sh4vita\game\sh4.exe @Rest --snapshot J:\sh4vita\re\out\sh4.snapshot
