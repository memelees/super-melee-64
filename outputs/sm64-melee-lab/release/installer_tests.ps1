# Asset-free checks: parse scripts and exercise pure argument/header helpers.
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'installer_windows_support.ps1')
$results=@()
foreach ($name in @('SetupWindows.ps1','Install Helper.ps1','installer_windows_support.ps1','installer_tests.ps1')) {
    $tokens=$null; $errors=$null
    [void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
    $results+=@{name=$name;parse_errors=0;tokens=$tokens.Count}
}
$cases=@(@('a b','"a b"'),@('x\','"x\\"'),@('x"y','"x\"y"'),@('','""'))
foreach ($case in $cases) { if ((ConvertTo-WindowsArgument $case[0]) -cne $case[1]) { throw "Argument quoting failed: $($case[0])" } }
$temporary=Join-Path ([IO.Path]::GetTempPath()) ('supermelee-installer-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($temporary)
try {
    $sm64=Join-Path $temporary 'owned rom'; $melee=Join-Path $temporary 'owned disc'; $other=Join-Path $temporary 'other'
    [IO.File]::WriteAllBytes($sm64,[byte[]](0x80,0x37,0x12,0x40,0,0,0,0))
    [IO.File]::WriteAllBytes($melee,[byte[]](0x47,0x41,0x4c,0x45,0x30,0x31,0,2))
    [IO.File]::WriteAllBytes($other,[byte[]](1,2,3))
    if ((Get-GameKind $sm64) -ne 'sm64' -or (Get-GameKind $melee) -ne 'melee' -or (Get-GameKind $other) -ne 'unsupported') { throw 'Game header classification failed.' }
    $nested=Join-Path $temporary 'outputs/sm64-melee-lab/native';[void][IO.Directory]::CreateDirectory($nested)
    [IO.File]::WriteAllText((Join-Path $nested 'bridge_protocol.h'),'asset-free fixture')
    if ((Get-SuperMeleeRoot $nested) -ne $temporary) { throw 'Source-kit root detection failed.' }
    $diagnostic=New-SetupDiagnostic 'Finish Ubuntu initialization and any Windows restart'
    if ($diagnostic.code -ne 'windows-wsl' -or $diagnostic.recovery -notmatch 'Start menu') { throw 'WSL repair instructions missing.' }
    $diagnostic=New-SetupDiagnostic 'Install the Microsoft Visual C++ x64 runtime'
    if ($diagnostic.code -ne 'windows-runtime' -or $diagnostic.recovery -notmatch 'Prepare Build Tools') { throw 'Runtime repair instructions missing.' }
    $work=Join-Path $temporary 'work';[void][IO.Directory]::CreateDirectory($work)
    $diagnostic | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $work 'setup-error.json') -Encoding UTF8
    if ((Get-SetupDiagnostic $temporary).code -ne 'windows-runtime') { throw 'Saved diagnostic was not read.' }
    [IO.File]::WriteAllText((Join-Path $temporary 'setup.log'),'old backend failure')
    [IO.File]::WriteAllText((Join-Path $temporary 'setup-events.log'),'new preparation failure')
    if ((Get-SetupLogPath $temporary) -ne (Join-Path $temporary 'setup-events.log')) { throw 'The log button selected a stale backend log.' }
    [IO.File]::WriteAllText((Join-Path $work 'setup-error.json'),'invalid json')
    if ($null -ne (Get-SetupDiagnostic $temporary)) { throw 'Invalid diagnostic JSON accepted.' }
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
@{passed=$true;parsing=$results;argument_cases=$cases.Count;game_headers=3;root_detection=$true;failure_diagnostics=$true;current_log_selected=$true;tools_installed=$false;games_started=$false}|ConvertTo-Json -Depth 5
