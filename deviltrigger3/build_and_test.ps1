<#
Build and test helper for devil_trigger_v3 and send_ready on Windows.

Usage:
  .\build_and_test.ps1 -OpalInclude "C:\OPAL-RT\RT-LAB\2021.3.4\common\include" -OpalLib "C:\OPAL-RT\RT-LAB\2021.3.4\common\lib"

The script will try MSVC `cl` first, then fall back to `g++` (MinGW). It builds `devil_trigger_v3.exe` and `send_ready.exe`.
#>

param(
    [string]$OpalInclude = "C:\\OPAL-RT\\RT-LAB\\2021.3.4\\common\\include",
    [string]$OpalLib = "C:\\OPAL-RT\\RT-LAB\\2021.3.4\\common\\lib",
    [string]$BuildDir = ".",
    [switch]$RunTest
)

Write-Host "Using OPAL include:" $OpalInclude
Write-Host "Using OPAL lib:" $OpalLib

function Run-Command($cmd) {
    Write-Host "-> $cmd"
    & cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "Command failed: $cmd" }
}

# Determine compiler
$clPath = & where.exe cl.exe 2>$null
if ($clPath) {
    Write-Host "MSVC detected. Building with cl.exe"
    $cl = "cl"
    $buildDevil = "cl /nologo /EHsc $BuildDir\\devil_trigger_v3.cpp ws2_32.lib OpalApi.lib /I\"$OpalInclude\" /link /LIBPATH:\"$OpalLib\" /OUT:$BuildDir\\devil_trigger_v3.exe"
    $buildSend = "cl /nologo /EHsc $BuildDir\\send_ready.cpp ws2_32.lib /OUT:$BuildDir\\send_ready.exe"
    Run-Command $buildDevil
    Run-Command $buildSend
}
else {
    Write-Host "MSVC not found; trying g++ (MinGW)."
    $gppPath = & where.exe g++.exe 2>$null
    if (-not $gppPath) { throw "No compiler found (cl or g++ required)." }
    $gpp = "g++"
    $buildDevil = "g++ -static -O2 -o $BuildDir\\devil_trigger_v3.exe $BuildDir\\devil_trigger_v3.cpp -lws2_32 -L\"$OpalLib\" -lOpalApi -I\"$OpalInclude\""
    $buildSend = "g++ -static -O2 -o $BuildDir\\send_ready.exe $BuildDir\\send_ready.cpp -lws2_32"
    Run-Command $buildDevil
    Run-Command $buildSend
}

Write-Host "Build completed. Binaries: devil_trigger_v3.exe, send_ready.exe"

if ($RunTest) {
    Write-Host "Running quick test: start devil_trigger_v3 and send READY_"
    Start-Process -NoNewWindow -FilePath .\devil_trigger_v3.exe
    Start-Sleep -Seconds 1
    Write-Host "Sending READY_ to localhost:5008"
    & .\send_ready.exe 127.0.0.1 5008
}
