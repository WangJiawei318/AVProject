$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$project = Split-Path -Parent $root
$dest = Join-Path $root "bin"

New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $project "MediaPlayer\dll\*.dll") -Destination $dest -Force
Copy-Item (Join-Path $project "VideoRecorder\dll\*.dll") -Destination $dest -Force

$mingwBin = "D:\Software\Qt\Tools\mingw730_32\bin"
foreach ($dll in @("libgcc_s_dw2-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $path = Join-Path $mingwBin $dll
    if (Test-Path $path) {
        Copy-Item $path -Destination $dest -Force
    }
}

$exe = Join-Path $dest "AVClient.exe"
$qtDeploy = "D:\Software\Qt\5.12.11\mingw73_32\bin\windeployqt.exe"
if ((Test-Path $exe) -and (Test-Path $qtDeploy)) {
    & $qtDeploy --debug $exe
}

Write-Host "Runtime DLLs copied to $dest"
