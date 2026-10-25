param(
    [string]$BuildDir = 'build',
    [Parameter(Mandatory = $true)][string]$QtDir,
    [Parameter(Mandatory = $true)][string]$MinGWDir,
    [Parameter(Mandatory = $true)][string]$NsisExe,
    [Parameter(Mandatory = $true)][string]$RarExe
)

$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$qtPath = (Resolve-Path -LiteralPath $QtDir).Path
$compilerPath = (Resolve-Path -LiteralPath $MinGWDir).Path
$nsisPath = (Resolve-Path -LiteralPath $NsisExe).Path
$rarPath = (Resolve-Path -LiteralPath $RarExe).Path
if ((Get-AuthenticodeSignature -LiteralPath $rarPath).Status -ne 'Valid') {
    throw 'The RAR tool must have a valid publisher signature.'
}
$outputPath = Join-Path $projectDir 'installer'
$stagingPath = Join-Path $buildPath 'release-staging'
$bundlePath = Join-Path $stagingPath 'VitaSync'
if (Test-Path -LiteralPath $bundlePath) {
    throw 'The release staging folder already exists. Review it before packaging again.'
}
$executable = Join-Path $buildPath 'vitasync.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    $executable = Join-Path $buildPath 'bin/vitasync.exe'
}
if (-not (Test-Path -LiteralPath $executable)) {
    $executable = Join-Path $buildPath 'desktop/vitasync.exe'
}
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build the Release application first.' }
$projectVersion = [regex]::Match((Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw),
    'VERSION\s+(\d+\.\d+\.\d+)').Groups[1].Value
if (-not $projectVersion) { throw 'Project version could not be read.' }
$releaseVersion = $projectVersion -replace '\.0$', ''
$stem = "VitaSync-$releaseVersion-Windows-x64"
$setupPath = Join-Path $outputPath "$stem-Setup.exe"
$rarArchivePath = Join-Path $outputPath "$stem-Portable.rar"
foreach ($artifact in @($setupPath, $rarArchivePath)) {
    if (Test-Path -LiteralPath $artifact) { throw "Release artifact already exists: $artifact" }
}

New-Item -ItemType Directory -Path $bundlePath -Force | Out-Null
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$originalPath = $env:PATH
try {
    $env:PATH = "$qtPath\bin;$compilerPath\bin;$originalPath"
    Copy-Item -LiteralPath $executable -Destination (Join-Path $bundlePath 'vitasync.exe')
    & (Join-Path $qtPath 'bin/windeployqt.exe') --verbose 0 --release --no-translations `
        --no-opengl-sw --no-system-d3d-compiler --compiler-runtime `
        --qmldir (Join-Path $projectDir 'desktop/qml') (Join-Path $bundlePath 'vitasync.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
    foreach ($dll in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
        Copy-Item -LiteralPath (Join-Path $compilerPath "bin/$dll") -Destination $bundlePath
    }
    $zlibPaths = @((Join-Path $qtPath 'bin/zlib1.dll'),
        (Join-Path $compilerPath 'bin/zlib1.dll'),
        (Join-Path $compilerPath 'x86_64-w64-mingw32/lib/zlib1.dll'))
    $zlibPath = $zlibPaths | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ($zlibPath) { Copy-Item -LiteralPath $zlibPath -Destination $bundlePath }
    New-Item -ItemType Directory -Path (Join-Path $bundlePath 'assets/covers') -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectDir 'assets/covers/titles.json') `
        -Destination (Join-Path $bundlePath 'assets/covers/titles.json')
    @'
[Paths]
Prefix=.
Plugins=.
Qml2Imports=qml
'@ | Set-Content -LiteralPath (Join-Path $bundlePath 'qt.conf') -Encoding ascii

    $licenseDir = Join-Path $bundlePath 'licenses'
    New-Item -ItemType Directory -Path $licenseDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.txt') -Destination $licenseDir
    foreach ($license in @('LGPL-3.0-only.txt', 'GPL-3.0-only.txt', 'Qt-GPL-exception-1.0.txt')) {
        Copy-Item -LiteralPath (Join-Path $projectDir "assets/licenses/qt/$license") -Destination $licenseDir
    }
    Copy-Item -LiteralPath (Join-Path $projectDir 'assets/icons/LUCIDE-LICENSE.txt') -Destination $licenseDir
    foreach ($component in @('gcc', 'winpthreads', 'mingw-w64', 'zlib')) {
        $componentLicenses = Join-Path $compilerPath "licenses/$component"
        if (Test-Path -LiteralPath $componentLicenses) {
            Copy-Item -LiteralPath $componentLicenses -Destination $licenseDir -Recurse
        }
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'RELEASE_NOTES.txt') -Destination $outputPath
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'RELEASE_NOTES.txt') `
        -Destination (Join-Path $bundlePath 'README.txt')
    & $nsisPath /V2 "/DSOURCE_DIR=$bundlePath" "/DOUT_FILE=$setupPath" `
        "/DAPP_VERSION=$projectVersion" (Join-Path $PSScriptRoot 'windows_installer.nsi')
    if ($LASTEXITCODE -ne 0) { throw 'NSIS compilation failed.' }
    Push-Location $stagingPath
    try {
        & $rarPath a -r -m5 -idq $rarArchivePath 'VitaSync'
        if ($LASTEXITCODE -ne 0) { throw 'RAR packaging failed.' }
    } finally {
        Pop-Location
    }
    & $rarPath t -idq $rarArchivePath
    if ($LASTEXITCODE -ne 0) { throw 'RAR verification failed.' }
    $checksums = foreach ($artifact in @($setupPath, $rarArchivePath, (Join-Path $outputPath 'RELEASE_NOTES.txt'))) {
        $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $(Split-Path -Leaf $artifact)"
    }
    $checksums | Set-Content -LiteralPath (Join-Path $outputPath 'SHA256SUMS.txt') -Encoding ascii
    Write-Output "Release files are ready in $outputPath (ignored by Git)."
} finally {
    $env:PATH = $originalPath
}
