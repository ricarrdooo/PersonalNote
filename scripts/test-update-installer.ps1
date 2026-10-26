$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $projectRoot ('build\update-installer-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$helper = Join-Path $PSScriptRoot 'update-installer.ps1'
$powershell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$results = @()
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Write-Text([string]$Path, [string]$Text) { [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null; [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding($false))) }
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function New-Fixture([string]$Name, [string]$Extra = '') {
    $root = Join-Path $testRoot $Name; $install = Join-Path $root 'program'; $job = Join-Path $root 'job'; $source = Join-Path $root 'source'
    [IO.Directory]::CreateDirectory($job) | Out-Null
    foreach ($file in @('Shiye.exe', 'a.dll', 'libstdc++-6.dll', 'locked.dll', 'update-installer.ps1')) { Write-Text (Join-Path $install $file) ('old-' + $file); Write-Text (Join-Path $source $file) ('new-' + $file) }
    Write-Text (Join-Path $install 'data\notes.sqlite') 'original notes, must not be modified'
    Write-Text (Join-Path $install 'data\attachments\image.png') 'original image'
    $files = @{}; foreach ($file in (Get-ChildItem -LiteralPath $source -File)) { $files[$file.Name] = Hash $file.FullName }
    $package = Join-Path $job 'Shiye-windows-x64.zip'; [IO.Compression.ZipFile]::CreateFromDirectory($source, $package)
    if ($Extra) {
        $zip = [IO.Compression.ZipFile]::Open($package, [IO.Compression.ZipArchiveMode]::Update)
        try { $entry = $zip.CreateEntry($Extra); $writer = New-Object IO.StreamWriter($entry.Open()); $writer.Write('unexpected'); $writer.Dispose() } finally { $zip.Dispose() }
    }
    $manifest = @{schema=1; version='v9.0.0'; archive='Shiye-windows-x64.zip'; sha256=(Hash $package); files=$files}
    Write-Text (Join-Path $job 'Shiye-update.json') (ConvertTo-Json $manifest -Depth 8)
    Write-Text (Join-Path $job 'plan.json') (ConvertTo-Json @{schema=1; version='v9.0.0'; currentVersion='8.0.0'; parentPid=0; installDirectory=$install; dataDirectory=(Join-Path $install 'data')})
    return @{root=$root; install=$install; job=$job; source=$source}
}
function Run-Helper($Fixture, [switch]$Recover) {
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $helper, '-NoRestart')
    if ($Recover) { $arguments += @('-RecoverInstall', $Fixture.install) } else { $arguments += @('-Plan', (Join-Path $Fixture.job 'plan.json')) }
    & $powershell @arguments *> (Join-Path $Fixture.root 'helper.log')
    return $LASTEXITCODE
}
function Check-Data($Fixture) { Assert ([IO.File]::ReadAllText((Join-Path $Fixture.install 'data\notes.sqlite')) -eq 'original notes, must not be modified') 'Notes were changed'; Assert ([IO.File]::ReadAllText((Join-Path $Fixture.install 'data\attachments\image.png')) -eq 'original image') 'Image was changed' }

$fixture = New-Fixture 'success'
Assert ((Run-Helper $fixture) -eq 0) 'Valid update failed'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.install 'Shiye.exe')) -eq 'new-Shiye.exe') 'New executable was not installed'; Check-Data $fixture
Assert ([IO.File]::ReadAllText((Join-Path $fixture.job 'rollback\Shiye.exe')) -eq 'old-Shiye.exe') 'Old program copy was not retained'
$results += @{check='install_preserves_notes_and_retains_old_program'; passed=$true}

$fixture = New-Fixture 'bad-hash'; [IO.File]::AppendAllText((Join-Path $fixture.job 'Shiye-windows-x64.zip'), 'tampering')
Assert ((Run-Helper $fixture) -ne 0) 'Corrupted archive was accepted'; Assert ([IO.File]::ReadAllText((Join-Path $fixture.install 'Shiye.exe')) -eq 'old-Shiye.exe') 'Bad hash changed installed files'; Check-Data $fixture
$results += @{check='corrupted_archive_is_rejected'; passed=$true}

$fixture = New-Fixture 'bad-file-hash'; $path = Join-Path $fixture.job 'Shiye-update.json'; $object = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; $object.files.'Shiye.exe' = ('0' * 64); Write-Text $path (ConvertTo-Json $object -Depth 8)
Assert ((Run-Helper $fixture) -ne 0) 'A corrupted program file was accepted'; Check-Data $fixture
$results += @{check='individual_program_file_hash_is_verified'; passed=$true}

$fixture = New-Fixture 'downgrade'; $path = Join-Path $fixture.job 'plan.json'; $object = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; $object.currentVersion='10.0.0'; Write-Text $path (ConvertTo-Json $object)
Assert ((Run-Helper $fixture) -ne 0) 'A downgrade was accepted'; Check-Data $fixture
$results += @{check='older_versions_cannot_replace_newer_programs'; passed=$true}

$fixture = New-Fixture 'custom-library'; Write-Text (Join-Path $fixture.install 'knowledge\private.txt') 'private note'; Write-Text (Join-Path $fixture.source 'knowledge\private.txt') 'must not replace private note'
$path = Join-Path $fixture.job 'plan.json'; $object = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; $object.dataDirectory=Join-Path $fixture.install 'knowledge'; Write-Text $path (ConvertTo-Json $object)
$package = Join-Path $fixture.job 'Shiye-windows-x64.zip'; [IO.File]::Delete($package); [IO.Compression.ZipFile]::CreateFromDirectory($fixture.source, $package)
$path = Join-Path $fixture.job 'Shiye-update.json'; $object = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json; $object.sha256=Hash $package; $object.files | Add-Member -NotePropertyName 'knowledge/private.txt' -NotePropertyValue (Hash (Join-Path $fixture.source 'knowledge\private.txt')); Write-Text $path (ConvertTo-Json $object -Depth 8)
Assert ((Run-Helper $fixture) -ne 0) 'A custom note directory was overwritten'; Assert ([IO.File]::ReadAllText((Join-Path $fixture.install 'knowledge\private.txt')) -eq 'private note') 'Private note was changed'; Check-Data $fixture
$results += @{check='custom_library_directory_is_protected'; passed=$true}

foreach ($entry in @('../escape.dll', 'data/notes.sqlite', 'extra.dll', 'shiye.exe')) {
    $fixture = New-Fixture ('unsafe-' + [Guid]::NewGuid().ToString('N')) $entry
    Assert ((Run-Helper $fixture) -ne 0) ('Unsafe entry was accepted: ' + $entry); Check-Data $fixture
    Assert ([IO.File]::ReadAllText((Join-Path $fixture.install 'Shiye.exe')) -eq 'old-Shiye.exe') 'Rejected ZIP changed installed files'
    $results += @{check=('reject_zip_entry_' + $entry); passed=$true}
}

$fixture = New-Fixture 'rollback'; $locked = Join-Path $fixture.install 'locked.dll'; [IO.File]::SetAttributes($locked, [IO.FileAttributes]::ReadOnly)
try { Assert ((Run-Helper $fixture) -ne 0) 'Simulated installation failure was not detected' } finally { [IO.File]::SetAttributes($locked, [IO.FileAttributes]::Normal) }
Assert ([IO.File]::ReadAllText((Join-Path $fixture.install 'a.dll')) -eq 'old-a.dll') 'Partial installation was not rolled back'; Check-Data $fixture
$results += @{check='failed_file_replacement_rolls_back_previous_files'; passed=$true}

$fixture = New-Fixture 'interrupted'; $old = Join-Path $fixture.job 'rollback\Shiye.exe'; Write-Text $old 'old-Shiye.exe'
Write-Text (Join-Path $fixture.install 'Shiye.exe') 'partially replaced exe'
Write-Text (Join-Path $fixture.job 'journal.json') (ConvertTo-Json @{schema=1; files=@(@{path='Shiye.exe'; existed=$true; hash=(Hash $old)})} -Depth 8)
Write-Text (Join-Path $fixture.install '.shiye-update-pending.json') (ConvertTo-Json @{schema=1; plan=(Join-Path $fixture.job 'plan.json')})
Assert ((Run-Helper $fixture -Recover) -eq 0) 'Interrupted update did not recover'
Assert ([IO.File]::ReadAllText((Join-Path $fixture.install 'Shiye.exe')) -eq 'old-Shiye.exe') 'Recovery did not restore old executable'; Check-Data $fixture
$results += @{check='interrupted_update_recovers_before_opening_library'; passed=$true}

Write-Text (Join-Path $testRoot 'results.json') (ConvertTo-Json @{passed=$true; checks=$results} -Depth 8)
Write-Output ('Installer checks passed: ' + $results.Count)
Write-Output ('Results: ' + $testRoot)
