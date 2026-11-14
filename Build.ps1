param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('x64','Win32')][string]$Platform = 'x64',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Install Visual Studio 2026 with the Desktop development with C++ workload first.'
}
$installation = & $vswhere -latest -version '[18.0,19.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {
    throw 'Visual Studio 2026 C++ tools were not found. Add the Desktop development with C++ workload in Visual Studio Installer.'
}
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe'
& $msbuild 'InkBall.sln' '/m' '/t:Build' "/p:Configuration=$Configuration" "/p:Platform=$Platform" '/verbosity:minimal' '/nologo'
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
if (-not $SkipTests) {
    foreach ($test in @('Engine', 'Catalog', 'Renderer')) {
        & (Join-Path $PSScriptRoot "bin\$Platform\$Configuration\InkBall.$($test)Tests.exe") (Join-Path $PSScriptRoot 'assets\levels.ikl')
        if ($LASTEXITCODE -ne 0) { throw "$test tests failed ($LASTEXITCODE)." }
    }
}
Write-Host "Built: $PSScriptRoot\bin\$Platform\$Configuration\inkball.exe"
