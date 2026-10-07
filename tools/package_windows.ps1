param(
    [string]$BuildDir = 'build',
    [Parameter(Mandatory = $true)][string]$QtDir,
    [Parameter(Mandatory = $true)][string]$MinGWDir,
    [Parameter(Mandatory = $true)][string]$NsisExe
)

$ErrorActionPreference = 'Stop'
$projectDir = Split-Path -Parent $PSScriptRoot
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$qtPath = (Resolve-Path -LiteralPath $QtDir).Path
$compilerPath = (Resolve-Path -LiteralPath $MinGWDir).Path
$nsisPath = (Resolve-Path -LiteralPath $NsisExe).Path
$outputPath = Join-Path $projectDir 'installer'
$bundlePath = Join-Path $outputPath 'VitaSync'
if (Test-Path -LiteralPath $bundlePath) {
    throw 'installer/VitaSync already exists. Move the previous bundle before packaging again.'
}
$executable = Join-Path $buildPath 'vitasync.exe'
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
$zipPath = Join-Path $outputPath "$stem-Portable.zip"
foreach ($artifact in @($setupPath, $zipPath)) {
    if (Test-Path -LiteralPath $artifact) { throw "Release artifact already exists: $artifact" }
}

New-Item -ItemType Directory -Path $bundlePath -Force | Out-Null
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
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/qt/qtbase/v6.8.3/LICENSES/$license" `
            -OutFile (Join-Path $licenseDir $license)
    }
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
    Compress-Archive -LiteralPath $bundlePath -DestinationPath $zipPath -CompressionLevel Optimal
    $checksums = foreach ($artifact in @($setupPath, $zipPath, (Join-Path $outputPath 'RELEASE_NOTES.txt'))) {
        $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $(Split-Path -Leaf $artifact)"
    }
    $checksums | Set-Content -LiteralPath (Join-Path $outputPath 'SHA256SUMS.txt') -Encoding ascii
    Write-Output "Release files are ready in $outputPath (ignored by Git)."
} finally {
    $env:PATH = $originalPath
}
