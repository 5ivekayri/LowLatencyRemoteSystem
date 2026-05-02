param(
    [ValidateSet("dummy", "dxgi")]
    [string]$Mode = "dxgi",
    [int]$Monitor = 0,
    [int]$Fps = 30,
    [string]$Token = "dev-token"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$hostExe = Join-Path $root "x64\Debug\remote_host.exe"
$clientExe = Join-Path $root "x64\Debug\remote_client.exe"

if (!(Test-Path $hostExe) -or !(Test-Path $clientExe)) {
    throw "Build Debug|x64 first in Visual Studio, then run this script."
}

Start-Process -FilePath $hostExe -WorkingDirectory $root -ArgumentList @(
    "--mode", $Mode,
    "--monitor", "$Monitor",
    "--fps", "$Fps",
    "--token", $Token,
    "--tcp-port", "48000",
    "--udp-port", "47991"
)

Start-Sleep -Milliseconds 800

Start-Process -FilePath $clientExe -WorkingDirectory $root -ArgumentList @(
    "--host", "127.0.0.1",
    "--token", $Token,
    "--tcp-port", "48000",
    "--udp-port", "48001"
)
