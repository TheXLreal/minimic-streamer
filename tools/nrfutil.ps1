# Keep the programming utility and its packages local to this checkout.
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path -Parent $PSScriptRoot
$toolDir = Join-Path $repoDir 'build/nrfutil'
$exe = Join-Path $toolDir 'nrfutil.exe'
$previousNrfutilHome = $env:NRFUTIL_HOME
try {
    New-Item -ItemType Directory -Force -Path $toolDir | Out-Null
    if (!(Test-Path -LiteralPath $exe)) {
        Write-Host 'Downloading nRF Util from Nordic Semiconductor...'
        Invoke-WebRequest 'https://files.nordicsemi.com/artifactory/swtools/external/nrfutil/executables/x86_64-pc-windows-msvc/nrfutil.exe' -OutFile "$exe.download"
        Move-Item -LiteralPath "$exe.download" -Destination $exe
    }
    $env:NRFUTIL_HOME = Join-Path $repoDir 'build/nrfutil-home'
    & $exe @args
    if ($LASTEXITCODE -ne 0) { throw "nRF Util failed with exit code $LASTEXITCODE" }
} finally {
    $env:NRFUTIL_HOME = $previousNrfutilHome
}
