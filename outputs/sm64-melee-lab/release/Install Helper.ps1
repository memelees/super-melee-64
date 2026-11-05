Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
. (Join-Path $PSScriptRoot 'installer_windows_support.ps1')
$gameRoot=Get-SuperMeleeRoot $PSScriptRoot
$form=New-Object Windows.Forms.Form
$form.Text='Install Super Melee 64 (Windows experimental)'; $form.Size=New-Object Drawing.Size(740,630)
$form.AllowDrop=$true; $form.StartPosition='CenterScreen'
$label=New-Object Windows.Forms.Label
$label.Text="Drop your original SM64 US ROM and Melee USA Rev 2 disc here.`r`nSetup builds locally; game files are never uploaded. Allow 12 GiB free; 20 GiB is recommended."
$label.Location=New-Object Drawing.Point(22,20); $label.Size=New-Object Drawing.Size(680,100)
$label.Font=New-Object Drawing.Font('Segoe UI',13); $form.Controls.Add($label)
function Button($text,$x,$y,$width) { $b=New-Object Windows.Forms.Button; $b.Text=$text; $b.Location=New-Object Drawing.Point($x,$y); $b.Size=New-Object Drawing.Size($width,34); $form.Controls.Add($b); return $b }
$choose=Button 'Choose Files...' 22 130 150
$build=Button 'Build / Resume' 494 130 206; $build.Enabled=$false
$tools=Button 'Prepare Build Tools' 22 178 180
$open=Button 'Open Setup Log' 214 178 156
$cancel=Button 'Cancel' 594 178 106; $cancel.Enabled=$false
$log=New-Object Windows.Forms.TextBox
$log.Multiline=$true; $log.ReadOnly=$true; $log.ScrollBars='Vertical'; $log.Location=New-Object Drawing.Point(22,232); $log.Size=New-Object Drawing.Size(678,340); $form.Controls.Add($log)
$script:rom=$null; $script:melee=$null; $script:buildProcess=$null; $script:mode=''; $script:lastTail=''
function Accept-GameFiles($files) {
    foreach ($file in $files) {
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        try { $kind=Get-GameKind $file }
        catch { $log.AppendText("Cannot read $file : $($_.Exception.Message)`r`n"); continue }
        if ($kind -eq 'melee') { $script:melee=$file }
        elseif ($kind -eq 'sm64') { $script:rom=$file }
        else { $log.AppendText("Unrecognized game file: $file`r`n") }
    }
    $label.Text="SM64: $script:rom`r`nMelee: $script:melee"; $build.Enabled=($null -ne $script:rom -and $null -ne $script:melee -and $null -eq $script:buildProcess)
}
function Start-Setup($prepare) {
    if ($null -ne $script:buildProcess) { return }
    $arguments=@('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',"$PSScriptRoot\SetupWindows.ps1")
    if ($prepare) { $arguments+='-PrepareTools'; $script:mode='tools' } else { $arguments+=@('-Rom',$script:rom,'-Melee',$script:melee); $script:mode='build' }
    try {
        $script:buildProcess=Start-Process powershell.exe -PassThru -ArgumentList (($arguments | ForEach-Object { ConvertTo-WindowsArgument $_ }) -join ' ')
        $build.Enabled=$false; $tools.Enabled=$false; $cancel.Enabled=!$prepare; $timer.Start()
        $log.AppendText("Setup opened in its own progress window. Prepare Build Tools may show Windows permission prompts. Inputs stay local.`r`n")
    } catch { $log.AppendText("Cannot start setup: $($_.Exception.Message)`r`n"); $script:buildProcess=$null }
}
function Cancel-Setup {
    if ($null -eq $script:buildProcess) { return }
    if ($script:mode -eq 'tools') { $log.AppendText("Finish or cancel the system package manager in its window before closing this helper.`r`n"); return }
    $work=Join-Path $gameRoot 'work'; [IO.Directory]::CreateDirectory($work) | Out-Null
    [IO.File]::WriteAllText((Join-Path $work 'setup.cancel'),'cancel')
    $cancel.Enabled=$false; $log.AppendText("Stopping safely. Verified downloads and compiler outputs are kept for Resume.`r`n")
}
$form.Add_DragEnter({ if ($_.Data.GetDataPresent([Windows.Forms.DataFormats]::FileDrop)) { $_.Effect='Copy' } })
$form.Add_DragDrop({ Accept-GameFiles $_.Data.GetData([Windows.Forms.DataFormats]::FileDrop) })
$choose.Add_Click({ $picker=New-Object Windows.Forms.OpenFileDialog; $picker.Multiselect=$true; $picker.Filter='Game images|*.z64;*.iso;*.gcm|All files|*.*'; try { if ($picker.ShowDialog() -eq 'OK') { Accept-GameFiles $picker.FileNames } } finally { $picker.Dispose() } })
$build.Add_Click({ Start-Setup $false }); $tools.Add_Click({ Start-Setup $true }); $cancel.Add_Click({ Cancel-Setup })
$open.Add_Click({ if (Test-Path "$gameRoot\setup.log") { Start-Process notepad.exe -ArgumentList (ConvertTo-WindowsArgument "$gameRoot\setup.log") } else { Start-Process explorer.exe -ArgumentList (ConvertTo-WindowsArgument $gameRoot) } })
$timer=New-Object Windows.Forms.Timer; $timer.Interval=750
$timer.Add_Tick({
    if (Test-Path "$gameRoot\setup.log") {
        $tail=(Get-Content -LiteralPath "$gameRoot\setup.log" -Tail 10 -ErrorAction SilentlyContinue) -join "`r`n"
        if ($tail -ne $script:lastTail) { $log.AppendText($tail+"`r`n"); $script:lastTail=$tail }
    }
    $script:buildProcess.Refresh()
    if ($script:buildProcess.HasExited) {
        $script:buildProcess.WaitForExit(); $code=$script:buildProcess.ExitCode; $timer.Stop(); $script:buildProcess=$null
        $tools.Enabled=$true; $cancel.Enabled=$false; Accept-GameFiles @()
        $log.AppendText("Setup process finished (exit $code). Build / Resume retries using verified caches. See setup.log or setup-events.log for details.`r`n")
    }
})
$form.Add_FormClosing({ if ($null -ne $script:buildProcess) { $_.Cancel=$true; Cancel-Setup } })
[void]$form.ShowDialog()
