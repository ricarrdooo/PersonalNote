param(
    [string]$QtRoot = 'F:\APP\QT5.12.12\5.12.12\mingw73_64',
    [string]$MinGWRoot = 'F:\APP\QT5.12.12\Tools\mingw730_64',
    [switch]$SkipDeploy
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$qmakePath = Join-Path $QtRoot 'bin\qmake.exe'
$makePath = Join-Path $MinGWRoot 'bin\mingw32-make.exe'
if (!(Test-Path -LiteralPath $qmakePath) -or !(Test-Path -LiteralPath $makePath)) {
    throw '没有找到 Qt / MinGW，请通过 -QtRoot 和 -MinGWRoot 指定安装目录。'
}
$env:PATH = "$(Join-Path $QtRoot 'bin');$(Join-Path $MinGWRoot 'bin');$env:PATH"
$buildDirectory = Join-Path $projectRoot 'build\desktop'
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
Push-Location $buildDirectory
try {
    & $qmakePath (Join-Path $projectRoot 'PersonalNote.pro') 'CONFIG+=release' 'CONFIG-=debug'
    if ($LASTEXITCODE -ne 0) { throw "qmake 失败：$LASTEXITCODE" }
    & $makePath '-j4'
    if ($LASTEXITCODE -ne 0) { throw "编译失败：$LASTEXITCODE" }
    $executable = Join-Path $buildDirectory 'release\Shiye.exe'
    if (!$SkipDeploy) {
        $releaseDirectory = Join-Path $projectRoot 'dist\Shiye'
        New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
        Copy-Item -LiteralPath $executable -Destination (Join-Path $releaseDirectory 'Shiye.exe') -Force
        Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party\md4c\LICENSE.md') -Destination (Join-Path $releaseDirectory 'LICENSE-MD4C.txt') -Force
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'update-installer.ps1') -Destination (Join-Path $releaseDirectory 'update-installer.ps1') -Force
        & (Join-Path $QtRoot 'bin\windeployqt.exe') '--release' '--no-translations' '--compiler-runtime' (Join-Path $releaseDirectory 'Shiye.exe')
        if ($LASTEXITCODE -ne 0) { throw "Qt 运行库打包失败：$LASTEXITCODE" }
        Write-Output "桌面程序：$(Join-Path $releaseDirectory 'Shiye.exe')"
    }
} finally { Pop-Location }

