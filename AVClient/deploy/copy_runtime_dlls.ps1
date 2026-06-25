$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$project = Split-Path -Parent $root
$dest = Join-Path $root "bin"

New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $project "MediaPlayer\dll\*.dll") -Destination $dest -Force
Copy-Item (Join-Path $project "VideoRecorder\dll\*.dll") -Destination $dest -Force

Write-Host "Runtime DLLs copied to $dest"
