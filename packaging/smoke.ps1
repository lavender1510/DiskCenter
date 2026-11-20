param(
    [Parameter(Mandatory)][ValidateSet("x86", "x64")][string] $Arch,
    [Parameter(Mandatory)][string] $PackageDir
)
$ErrorActionPreference = 'Stop'
$version = (Get-Content (Join-Path $PSScriptRoot '../VERSION') -Raw).Trim()
$stem = "DiscCenter-$version"
$packagePath = (Resolve-Path $PackageDir).Path
$root = Join-Path $env:TEMP ("DiscCenter-smoke-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $root | Out-Null
function Invoke-Checked([string] $Program, [string] $Arguments) {
    $process = Start-Process -FilePath $Program -ArgumentList $Arguments -PassThru
    if (-not $process.WaitForExit(60000)) {
        $process.Kill()
        throw "Timed out running $Program"
    }
    if ($process.ExitCode -ne 0) { throw "Exit $($process.ExitCode) running $Program" }
}
function Test-App([string] $From, [string] $File) {
    Invoke-Checked (Join-Path $From 'DiscCenter.exe') "--smoke-test `"$File`""
    $result = Get-Content $File -Raw
    if ($result -notmatch "(?m)^arch=$Arch\s*$" -or
        $result -notmatch "(?m)^version=$([regex]::Escape($version))\s*$" -or
        $result -notmatch '(?m)^working_set=[1-9][0-9]*\s*$') {
        throw "Unexpected smoke-test result: $result"
    }
    Write-Output $result
}
$installed = Join-Path $root 'installed'
$uninstaller = Join-Path $installed 'Uninstall.exe'
try {
    $portable = Join-Path $root 'portable'
    Expand-Archive -LiteralPath (Join-Path $packagePath "$stem-Portable-$Arch.zip") -DestinationPath $portable
    Test-App $portable (Join-Path $root 'portable-result.txt')
    if (-not (Test-Path (Join-Path $portable 'activity.log'))) { throw 'Portable log missing' }
    Invoke-Checked (Join-Path $packagePath "$stem-Setup-$Arch.exe") "/S /D=$installed"
    Test-App $installed (Join-Path $root 'installed-result.txt')
    Invoke-Checked $uninstaller "/S _?=$installed"
    if (Test-Path (Join-Path $installed 'DiscCenter.exe')) { throw 'Uninstall left the program executable' }
} finally {
    if ((Test-Path $uninstaller) -and (Test-Path (Join-Path $installed 'DiscCenter.exe'))) {
        Invoke-Checked $uninstaller "/S _?=$installed"
    }
    Remove-Item $root -Recurse -Force
}
