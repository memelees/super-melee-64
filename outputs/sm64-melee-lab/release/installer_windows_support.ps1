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
