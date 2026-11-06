param([string]$Rom,[string]$Melee,[switch]$PrepareTools,[string]$LinuxDistro='Ubuntu-24.04')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
$scripts = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $scripts 'installer_windows_support.ps1')
$root=Get-SuperMeleeRoot $scripts
if ($LinuxDistro -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Invalid Linux distribution name.' }
Start-Transcript -Path (Join-Path $root 'setup-events.log') -Append | Out-Null
try {
    $diagnosticPath=Join-Path $root 'work/setup-error.json'
    if (Test-Path -LiteralPath $diagnosticPath) { Remove-Item -LiteralPath $diagnosticPath }
    $version=(Select-String -LiteralPath (Join-Path $root 'outputs/sm64-melee-lab/native/package_release.py') -Pattern "^VERSION='([^']+)'" | Select-Object -First 1).Matches.Groups[1].Value
    Write-Host "SUPER MELEE 64 INSTALLER $version - $([DateTimeOffset]::Now.ToString('o'))"
    if ($PrepareTools) {
        Write-Host 'Preparing the Microsoft runtime and Ubuntu build tools. Windows may request permission or a restart.'
        $vc = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' -ErrorAction SilentlyContinue
        if (!$vc -or $vc.Installed -ne 1) {
            if (!(Get-Command winget.exe -ErrorAction SilentlyContinue)) { Start-Process 'https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist'; throw 'Install the official x64 runtime, then choose Prepare Build Tools again.' }
            & winget.exe install --id Microsoft.VCRedist.2015+.x64 --exact --source winget --accept-source-agreements --accept-package-agreements
            if ($LASTEXITCODE -ne 0) { throw 'The Microsoft runtime installer stopped. Complete its permission prompt, then retry.' }
        }
        if (!(Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
            Start-Process 'https://learn.microsoft.com/en-us/windows/wsl/install'
            throw 'Install WSL and Ubuntu 24.04 using Microsoft instructions. Restart if requested, then reopen setup.'
        }
        $distros = @((& wsl.exe --list --quiet) | ForEach-Object { ($_ -replace "`0",'').Trim() })
        if ($LASTEXITCODE -ne 0 -or $distros -notcontains $LinuxDistro) {
            & wsl.exe --install --distribution $LinuxDistro
            throw 'Finish Ubuntu initialization and any Windows restart, then choose Prepare Build Tools again.'
        }
        & wsl.exe --distribution $LinuxDistro --user root --exec apt-get update
        if ($LASTEXITCODE -ne 0) { throw 'Open Ubuntu once and finish its initial account setup, then retry.' }
        & wsl.exe --distribution $LinuxDistro --user root --exec apt-get install -y build-essential git cmake ninja-build pkg-config curl patch xdelta3 python3 python3-pil p7zip-full g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64
        if ($LASTEXITCODE -ne 0) { throw 'Ubuntu tool installation stopped. Its downloaded packages are kept; retry Prepare Build Tools.' }
        & wsl.exe --distribution $LinuxDistro --user root --exec update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
        if ($LASTEXITCODE -ne 0) { throw 'Cannot select the POSIX MinGW C compiler.' }
        & wsl.exe --distribution $LinuxDistro --user root --exec update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
        if ($LASTEXITCODE -ne 0) { throw 'Cannot select the POSIX MinGW C++ compiler.' }
        Write-Host 'Build tools prepared. Return to the installer and choose Build / Resume.'
        exit 0
    }
    $vc = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64' -ErrorAction SilentlyContinue
    if (!$vc -or $vc.Installed -ne 1 -or !(Test-Path "$env:WINDIR\System32\MSVCP140_ATOMIC_WAIT.dll")) { throw 'Choose Prepare Build Tools to install the Microsoft Visual C++ x64 runtime.' }
    if (!(Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'Choose Prepare Build Tools to install WSL / Ubuntu 24.04.' }
    function Pick-GameFile([string]$title,[string]$filter) {
        $picker=New-Object System.Windows.Forms.OpenFileDialog
        try { $picker.Title=$title; $picker.Filter=$filter; if ($picker.ShowDialog() -ne 'OK') { throw 'Setup cancelled; inputs are unchanged.' }; return $picker.FileName }
        finally { $picker.Dispose() }
    }
    function Linux-Path([string]$path) {
        $result=& wsl.exe --distribution $LinuxDistro --exec wslpath -u ([IO.Path]::GetFullPath($path))
        if ($LASTEXITCODE -ne 0 -or !$result) { throw 'Ubuntu could not open that path. Finish its initialization, then retry.' }
        return ($result -join "`n").Trim()
    }
    if (!$Rom) { $Rom=Pick-GameFile 'Select your original US Super Mario 64 ROM' 'SM64 ROM (*.z64)|*.z64|All files (*.*)|*.*' }
    if (!$Melee) { $Melee=Pick-GameFile 'Select your original Melee USA Rev 2 disc' 'GameCube image (*.iso;*.gcm)|*.iso;*.gcm|All files (*.*)|*.*' }
    $linuxRoot=Linux-Path $root; $linuxRom=Linux-Path $Rom; $linuxMelee=Linux-Path $Melee
    $cancelFile=Join-Path $root 'work/setup.cancel'
    if (Test-Path -LiteralPath $cancelFile) { Remove-Item -LiteralPath $cancelFile }
    $linuxSetup=Linux-Path (Join-Path $scripts 'Setup.py')
    & wsl.exe --distribution $LinuxDistro --exec python3 $linuxSetup --platform windows --rom $linuxRom --melee $linuxMelee --cancel-file "$linuxRoot/work/setup.cancel" 2>&1 | Tee-Object -FilePath "$root/setup-console.log"
    if ($LASTEXITCODE -eq 130) { exit 130 }
    if ($LASTEXITCODE -ne 0) { throw 'Setup stopped. See setup.log. Build / Resume keeps verified downloads and compiler outputs.' }
    $shortcuts=New-Object -ComObject WScript.Shell; $link=$shortcuts.CreateShortcut("$root\Super Melee 64.lnk")
    $link.TargetPath="$root\Super Melee 64.exe"; $link.WorkingDirectory=$root
    $link.IconLocation="$root\outputs\sm64-melee-lab\branding\Super Melee 64.ico"; $link.Save()
    Write-Host 'Ready. Open Super Melee 64.lnk. Gameplay no longer needs Python or WSL.'
} catch {
    $diagnostic=Get-SetupDiagnostic $root
    if (!$diagnostic) { $diagnostic=New-SetupDiagnostic $_.Exception.Message }
    if (!$diagnostic.version) { $diagnostic | Add-Member -NotePropertyName version -NotePropertyValue $version -Force }
    try {
        [void][IO.Directory]::CreateDirectory((Join-Path $root 'work'))
        $diagnostic | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'work/setup-error.json') -Encoding UTF8
    } catch { } # A full or read-only disk must not hide the original explanation.
    Write-Host (Format-SetupDiagnostic $diagnostic)
    exit 1
} finally { Stop-Transcript | Out-Null }
