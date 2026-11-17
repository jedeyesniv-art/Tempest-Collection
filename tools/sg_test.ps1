# Space Giraffe helper test: installs build\sg\d3d9.dll + the given settings, starts the game through Steam, waits,
# takes a screenshot (logs\sg_<name>.png, 1/3 size), prints the helper's log and the game's log tail, closes the game.
# powershell -ExecutionPolicy Bypass -File tools\sg_test.ps1 -Name a -Settings "borderless = 0" [-Wait 30] [-Keep]
param([string]$Name = "t", [string]$Settings = "", [int]$Wait = 30, [switch]$Keep)
$g = "C:\Program Files (x86)\Steam\steamapps\common\Space Giraffe"
Stop-Process -Name SpaceGiraffePC -Force -ErrorAction SilentlyContinue
Start-Sleep 2
Copy-Item "$PSScriptRoot\..\build\sg\d3d9.dll" $g -Force
Set-Content -Path "$g\sg_helper.txt" -Value ($Settings -split ';' | ForEach-Object { $_.Trim() }) -Encoding ascii
Start-Process "steam://rungameid/27800"
Start-Sleep -Seconds $Wait
Add-Type -TypeDefinition 'using System.Runtime.InteropServices; public class SgDpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }'
[SgDpi]::SetProcessDPIAware() | Out-Null
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
[System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
(New-Object System.Drawing.Bitmap $bmp, ($b.Width / 3), ($b.Height / 3)).Save("$PSScriptRoot\..\logs\sg_$Name.png")
Get-Process SpaceGiraffePC -ErrorAction SilentlyContinue | ForEach-Object { "process: $($_.MainWindowTitle)" }
"--- helper log"
Get-Content "$g\sg_helper.log"
"--- game log (tail)"
Get-Content "$g\SGPCLogFile.txt" | Select-Object -Last 6
if (-not $Keep) { Stop-Process -Name SpaceGiraffePC -Force -ErrorAction SilentlyContinue }
