param([string]$Version, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $projectRoot 'src\main.cpp') -Raw
$sourceVersion = [regex]::Match($source, 'setApplicationVersion\(QStringLiteral\("([0-9.]+)"\)\)').Groups[1].Value
if (!$Version) { $Version = $sourceVersion }
$Version = $Version.TrimStart('v')
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw 'Version must use major.minor.patch.' }
if ($Version -ne $sourceVersion) { throw 'Release version must match the compiled application version.' }
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1'); if ($LASTEXITCODE -ne 0) { throw 'Build failed.' } }
$program = Join-Path $projectRoot 'dist\Shiye'
if (!(Test-Path -LiteralPath (Join-Path $program 'update-installer.ps1'))) { throw 'Build and deploy the updater first.' }
$output = Join-Path $projectRoot ('build\releases\v' + $Version + '-' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $output -Force | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = Join-Path $output 'Shiye-windows-x64.zip'; $hashes = [ordered]@{}
$stream = [IO.File]::Open($archive, [IO.FileMode]::CreateNew)
$zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
function Program-Files([string]$Directory) {
    foreach ($entry in (Get-ChildItem -LiteralPath $Directory -Force)) {
        if ($entry.Name.StartsWith('.') -or $entry.Name -match '^(data|attachments|backups|notes\.sqlite|preferences\.ini|library\.lock)$') { continue }
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Linked program path: ' + $entry.FullName) }
        if ($entry.PSIsContainer) { Program-Files $entry.FullName } else { $entry }
    }
}
try {
    foreach ($file in (Program-Files $program | Sort-Object FullName)) {
        $relative = $file.FullName.Substring($program.Length + 1).Replace('\', '/')
        if ($relative -match '(^|/)(data|attachments|backups)(/|$)' -or $relative -match '(^|/)(notes\.sqlite|preferences\.ini|library\.lock)$' -or $relative.StartsWith('.')) { continue }
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Linked program file: ' + $relative) }
        $hashes[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $zip.Dispose(); $stream.Dispose() }
$manifest = [ordered]@{schema=1; version=('v' + $Version); archive='Shiye-windows-x64.zip'; sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant(); files=$hashes}
$encoding = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText((Join-Path $output 'Shiye-update.json'), (ConvertTo-Json -InputObject $manifest -Depth 8), $encoding)
Write-Output ('Release tag: v' + $Version)
Write-Output ('Upload both files to GitHub Releases: ' + $output)
