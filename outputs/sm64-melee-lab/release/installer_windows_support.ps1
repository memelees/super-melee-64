# Pure installer helpers. No network, package installation, UI or game launch.
function ConvertTo-WindowsArgument([string]$Value) {
    if ($Value.Contains([char]0)) { throw 'An argument contains a NUL character.' }
    $out=New-Object Text.StringBuilder
    [void]$out.Append('"'); $slashes=0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        $count=$slashes
        if ($character -eq '"') { $count=2*$slashes+1 }
        [void]$out.Append(('\' * $count)); $slashes=0
        [void]$out.Append($character)
    }
    [void]$out.Append(('\' * (2*$slashes))); [void]$out.Append('"')
    return $out.ToString()
}
function Get-SuperMeleeRoot([string]$Path) {
    $current=[IO.Path]::GetFullPath($Path)
    while (!(Test-Path -LiteralPath (Join-Path $current 'outputs/sm64-melee-lab/native/bridge_protocol.h'))) {
        $parent=Split-Path -Parent $current
        if (!$parent -or $parent -eq $current) { throw 'Keep the installer with its complete extracted source kit.' }
        $current=$parent
    }
    return $current
}
function Get-GameKind([string]$Path) {
    $stream=[IO.File]::OpenRead($Path)
    try { $header=[byte[]]::new(8); $count=$stream.Read($header,0,8) }
    finally { $stream.Dispose() }
    if ($count -eq 8 -and [Text.Encoding]::ASCII.GetString($header,0,6) -eq 'GALE01' -and $header[7] -eq 2) { return 'melee' }
    if ($count -ge 4 -and [BitConverter]::ToString($header,0,4) -eq '80-37-12-40') { return 'sm64' }
    return 'unsupported'
}
function Get-SetupDiagnostic([string]$Root) {
    $path=Join-Path $Root 'work/setup-error.json'
    try {
        if (!(Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -gt 65536) { return $null }
        $value=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        foreach ($field in @('code','title','message','recovery')) {
            if ($value.$field -isnot [string] -or !$value.$field) { return $null }
        }
        return $value
    } catch { return $null }
}
function New-SetupDiagnostic([string]$Message) {
    $code='windows-setup';$title='Setup could not continue'
    $recovery='Open Setup Log and share the latest attempt, installer version and error code with the project maintainer. Build / Resume keeps verified downloads and completed outputs.'
    if ($Message -match 'Microsoft.*runtime|Visual C\+\+|MSVCP140') {
        $code='windows-runtime';$title='Install the Microsoft Visual C++ x64 runtime'
        $recovery='Choose Prepare Build Tools and finish the Microsoft runtime installer. Then choose Build / Resume. If Windows requests a restart, restart first.'
    } elseif ($Message -match 'WSL|Ubuntu|Linux distribution|virtualization') {
        $code='windows-wsl';$title='Finish WSL and Ubuntu setup'
        $recovery='Open Ubuntu 24.04 from the Start menu and finish its initial account setup. Restart Windows if requested, then choose Prepare Build Tools and Build / Resume.'
    } elseif ($Message -match 'without spaces') {
        $code='build-path';$title='Choose a build folder without spaces'
        $recovery='Extract the complete source kit to a writable folder such as C:\SuperMelee64, then reopen its installer. Your game filenames may contain spaces.'
    } elseif ($Message -match 'No space left|not enough space|GiB is available') {
        $code='disk-space';$title='More free disk space is needed'
        $recovery='Free space on the build drive. Setup requires 12 GiB free; 20 GiB is recommended. Then choose Build / Resume. Verified downloads are kept.'
    } elseif ($Message -match 'Access.*denied|permission denied|not writable') {
        $code='folder-access';$title='Setup cannot write a required file'
        $recovery='Use a writable build folder owned by your account. Check the drive is writable and has free space. Do not run the game setup as administrator.'
    }
    return [pscustomobject]@{code=$code;title=$title;message=$Message.Substring(0,[Math]::Min(2000,$Message.Length));recovery=$recovery}
}
function Format-SetupDiagnostic($Value) {
    $version=if ($Value.version) { $Value.version } else { 'See the installer window title' }
    return "$($Value.title)`r`n$($Value.message)`r`n`r`n$($Value.recovery)`r`n`r`nError code: $($Value.code)`r`nInstaller version: $version"
}
function Get-SetupLogPath([string]$Root) {
    # The Windows transcript includes preparation and pre-backend failures.
    foreach ($name in @('setup-events.log','setup-console.log','setup.log')) {
        $path=Join-Path $Root $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
    return $null
}
