Set shell = CreateObject("WScript.Shell")
Set fs = CreateObject("Scripting.FileSystemObject")
root = fs.GetParentFolderName(WScript.ScriptFullName)
launcher = root & "\Super Melee 64.exe"
If Not fs.FileExists(launcher) Then
  MsgBox "Run Setup Windows.cmd first to build Super Melee 64.", 48, "Super Melee 64"
Else
  shell.Run Chr(34) & launcher & Chr(34), 1, False
End If
