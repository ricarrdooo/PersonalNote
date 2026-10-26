param(
    [string]$Plan,
    [string]$RecoverInstall,
    [int]$WaitForProcessId = 0,
    [switch]$VerifyOnly,
    [switch]$NoRestart
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Full-Path([string]$Value) { return [IO.Path]::GetFullPath($Value).TrimEnd('\', '/') }
function Is-Within([string]$Child, [string]$Parent) { return $Child.StartsWith($Parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) }
function File-Hash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}
function Write-Json([string]$Path, $Value) {
    $temporary = $Path + '.tmp-' + [Guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($temporary, (ConvertTo-Json -InputObject $Value -Depth 12), (New-Object Text.UTF8Encoding($false)))
    if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) } else { [IO.File]::Move($temporary, $Path) }
}
function Safe-Relative([string]$Relative) {
    if ($Relative.Length -gt 220 -or $Relative -cnotmatch '^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$') { return $false }
    foreach ($part in $Relative.Split('/')) {
        if ($part.StartsWith('.') -or $part.EndsWith('.') -or $part -match '^(data|attachments|backups|notes\.sqlite|preferences\.ini|library\.lock)$' -or $part -match '^(con|prn|aux|nul|com[1-9]|lpt[1-9])($|\.)') { return $false }
    }
    return $true
}
function Destination([string]$Root, [string]$Relative) {
    if (!(Safe-Relative $Relative)) { throw ('Unsafe package path: ' + $Relative) }
    $target = Full-Path (Join-Path $Root $Relative)
    if (!(Is-Within $target $Root)) { throw 'Package path escapes installation directory.' }
    return $target
}
function Check-Reparse([string]$Path, [string]$Root) {
    $current = $Path
    while ($current.Length -ge $Root.Length) {
        if ([IO.File]::Exists($current) -or [IO.Directory]::Exists($current)) {
            if (([IO.File]::GetAttributes($current) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('Linked paths are not supported: ' + $current) }
        }
        if ($current.Equals($Root, [StringComparison]::OrdinalIgnoreCase)) { break }
        $current = [IO.Path]::GetDirectoryName($current)
        if (!$current) { break }
    }
}
function Atomic-Copy([string]$Source, [string]$Target) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Target)) | Out-Null
    $temporary = $Target + '.shiye-' + [Guid]::NewGuid().ToString('N')
    try {
        [IO.File]::Copy($Source, $temporary, $false)
        if ([IO.File]::Exists($Target)) { [IO.File]::Replace($temporary, $Target, [NullString]::Value) } else { [IO.File]::Move($temporary, $Target) }
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}
function Wait-Parent([int]$Id) {
    if ($Id -le 0) { return }
    $process = Get-Process -Id $Id -ErrorAction SilentlyContinue
    if ($process -and !$process.WaitForExit(120000)) { throw 'Shiye is still running; no files were installed.' }
}
function Rollback($Journal, [string]$Install, [string]$DataRoot, [string]$BackupRoot) {
    foreach ($entry in $Journal.files) {
        $target = Destination $Install ([string]$entry.path)
        if ($target.Equals($DataRoot, [StringComparison]::OrdinalIgnoreCase) -or (Is-Within $target $DataRoot)) { throw 'Rollback attempted to write into the note library.' }
        Check-Reparse $target $Install
        if ($entry.existed) {
            $old = Destination $BackupRoot ([string]$entry.path)
            if (!(Test-Path -LiteralPath $old -PathType Leaf) -or (File-Hash $old) -ne $entry.hash) { throw ('Rollback copy is missing or damaged: ' + $entry.path) }
            if ((Test-Path -LiteralPath $target -PathType Leaf) -and (File-Hash $target) -eq $entry.hash) { continue }
            Atomic-Copy $old $target
        } elseif ([IO.File]::Exists($target)) { [IO.File]::Delete($target) }
    }
}
function Quote-Argument([string]$Value) { return '"' + [regex]::Replace($Value, '(\\*)"', '$1$1\"').TrimEnd('\') + ('\' * ($Value.Length - $Value.TrimEnd('\').Length) * 2) + '"' }

$job = $null; $install = $null; $marker = $null; $journal = $null; $config = $null; $dataRoot = ''; $success = $false; $message = ''
try {
    if ($RecoverInstall) {
        $install = Full-Path $RecoverInstall
        $marker = Join-Path $install '.shiye-update-pending.json'
        if (!(Test-Path -LiteralPath $marker)) { exit 0 }
        $pending = [IO.File]::ReadAllText($marker) | ConvertFrom-Json
        $Plan = [string]$pending.plan
    }
    if (!$Plan -or !(Test-Path -LiteralPath $Plan -PathType Leaf)) { throw 'Update plan is missing.' }
    $Plan = Full-Path $Plan; $job = [IO.Path]::GetDirectoryName($Plan)
    $config = [IO.File]::ReadAllText($Plan) | ConvertFrom-Json
    if ($config.schema -ne 1) { throw 'Unsupported update plan.' }
    $configuredInstall = Full-Path ([string]$config.installDirectory)
    if ($install -and !$configuredInstall.Equals($install, [StringComparison]::OrdinalIgnoreCase)) { throw 'Recovery installation directory does not match.' }
    $install = $configuredInstall; $dataRoot = Full-Path ([string]$config.dataDirectory)
    if ($job.Equals($install, [StringComparison]::OrdinalIgnoreCase) -or (Is-Within $job $install)) { throw 'Update cache must be separate from the installation.' }
    Check-Reparse $install $install
    $marker = Join-Path $install '.shiye-update-pending.json'
    $journalPath = Join-Path $job 'journal.json'; $backup = Join-Path $job 'rollback'
    if ($RecoverInstall) {
        Wait-Parent $WaitForProcessId
        $journal = [IO.File]::ReadAllText($journalPath) | ConvertFrom-Json
        Rollback $journal $install $dataRoot $backup
        [IO.File]::Delete($marker); $message = 'An interrupted update was rolled back. Your note library was preserved.'
    } else {
        $manifest = [IO.File]::ReadAllText((Join-Path $job 'Shiye-update.json')) | ConvertFrom-Json
        if ($manifest.schema -ne 1 -or $manifest.archive -cne 'Shiye-windows-x64.zip' -or $manifest.version -cne $config.version -or $manifest.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid package manifest.' }
        if ([version](([string]$manifest.version).TrimStart('v')) -le [version](([string]$config.currentVersion).TrimStart('v'))) { throw 'The update must be newer than the installed version.' }
        $package = Join-Path $job 'Shiye-windows-x64.zip'
        if ((File-Hash $package) -cne $manifest.sha256) { throw 'Archive SHA-256 verification failed.' }
        $expected = @{}
        foreach ($property in $manifest.files.PSObject.Properties) {
            if (!(Safe-Relative $property.Name) -or $expected.ContainsKey($property.Name) -or [string]$property.Value -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid file manifest.' }
            $target = Destination $install $property.Name
            if ($target.Equals($dataRoot, [StringComparison]::OrdinalIgnoreCase) -or (Is-Within $target $dataRoot)) { throw 'Package attempted to replace note-library files.' }
            Check-Reparse $target $install; $expected[$property.Name] = [string]$property.Value
        }
        if (!$expected.ContainsKey('Shiye.exe') -or !$expected.ContainsKey('update-installer.ps1') -or $expected.Count -gt 2000) { throw 'Package is missing required program files.' }
        $stage = Join-Path $job ('stage-' + [Guid]::NewGuid().ToString('N')); [IO.Directory]::CreateDirectory($stage) | Out-Null
        $zip = [IO.Compression.ZipFile]::OpenRead($package); $seen = @{}; [long]$total = 0
        try {
            foreach ($entry in $zip.Entries) {
                $relative = $entry.FullName
                if ($relative.EndsWith('/')) { if (!(Safe-Relative $relative.TrimEnd('/'))) { throw 'Unsafe ZIP directory.' }; continue }
                if (!$expected.ContainsKey($relative) -or $seen.ContainsKey($relative) -or !(Safe-Relative $relative)) { throw ('Unexpected ZIP entry: ' + $relative) }
                if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000 -or ($entry.ExternalAttributes -band 0x400) -ne 0) { throw 'ZIP contains a linked file.' }
                $total += $entry.Length; if ($total -gt 2147483648) { throw 'Uncompressed update exceeds size limit.' }
                $path = Destination $stage $relative; [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $false)
                if ((File-Hash $path) -cne $expected[$relative]) { throw ('Program-file SHA-256 verification failed: ' + $relative) }
                $seen[$relative] = $true
            }
        } finally { $zip.Dispose() }
        if ($seen.Count -ne $expected.Count) { throw 'ZIP is incomplete.' }
        if ($VerifyOnly) { Write-Output 'Update package verified.'; exit 0 }
        Wait-Parent ([int]$config.parentPid)
        if ([IO.File]::Exists((Join-Path $job 'cancel'))) { throw 'Update was cancelled.' }
        $files = @()
        foreach ($relative in ($expected.Keys | Sort-Object)) {
            $target = Destination $install $relative; $existed = [IO.File]::Exists($target); $oldHash = ''
            if ($existed) { $oldHash = File-Hash $target; $old = Destination $backup $relative; [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($old)) | Out-Null; [IO.File]::Copy($target, $old, $false); if ((File-Hash $old) -ne $oldHash) { throw 'Previous program could not be backed up.' } }
            $files += @{path=$relative; existed=$existed; hash=$oldHash}
        }
        $journal = @{schema=1; files=$files}; Write-Json $journalPath $journal
        Write-Json $marker @{schema=1; plan=$Plan}
        foreach ($relative in ($expected.Keys | Sort-Object { if ($_ -eq 'update-installer.ps1') { 'zzz-' + $_ } else { $_ } })) { Atomic-Copy (Destination $stage $relative) (Destination $install $relative) }
        [IO.File]::Delete($marker); $success = $true; $message = 'Update installed successfully.'
    }
} catch {
    $message = $_.Exception.Message
    if ($journal -and $marker -and [IO.File]::Exists($marker)) {
        try { Rollback $journal $install $dataRoot $backup; [IO.File]::Delete($marker); $message += ' The previous program was restored.' }
        catch { $message += ' Rollback needs recovery: ' + $_.Exception.Message }
    }
} finally {
    if ($job -and !$VerifyOnly) { try { Write-Json (Join-Path $job 'result.json') @{ok=$success; message=$message; version=[string]$config.version} } catch {} }
}
if (!$VerifyOnly -and !$NoRestart -and $install -and !(Test-Path -LiteralPath $marker)) {
    try {
        $arguments = '--data-dir ' + (Quote-Argument $dataRoot) + ' --update-result ' + (Quote-Argument (Join-Path $job 'result.json'))
        Start-Process -FilePath (Join-Path $install 'Shiye.exe') -ArgumentList $arguments -WindowStyle Normal | Out-Null
    } catch { $message += ' Restart Shiye using its launcher.' }
}
if (!$success -and !$RecoverInstall) { Write-Error $message -ErrorAction Continue; exit 1 }
if ($RecoverInstall -and (Test-Path -LiteralPath $marker)) { Write-Error $message -ErrorAction Continue; exit 1 }
exit 0
