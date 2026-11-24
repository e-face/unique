# Unique installer. Sits next to payload.zip and arch.txt in the IExpress temp folder.
$ErrorActionPreference = 'Stop'

function Test-Admin
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin))
{
    $arg = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '"'
    Start-Process -FilePath 'powershell.exe' -ArgumentList $arg -Verb RunAs -Wait | Out-Null
    exit 0
}

$here = Split-Path -Parent $PSCommandPath
$arch = (Get-Content -LiteralPath (Join-Path $here 'arch.txt') -Encoding ASCII).Trim()
$version = '0.4.3'
if ($arch -eq 'x64')
{
    $dest = Join-Path $env:ProgramFiles 'Unique'
    $display = 'Unique (64-bit)'
    $keyName = 'Unique-0.4.3-x64'
}
else
{
    $programFiles32 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrEmpty($programFiles32))
        $programFiles32 = $env:ProgramFiles
    $dest = Join-Path $programFiles32 'Unique'
    $display = 'Unique (32-bit)'
    $keyName = 'Unique-0.4.3-x86'
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null
$zip = Join-Path $here 'payload.zip'
if (-not (Test-Path -LiteralPath $zip))
    throw "Не найден пакет: $zip"
tar -xf $zip -C $dest

$uninstall = Join-Path $dest 'uninstall.ps1'
@'
$ErrorActionPreference = "Stop"
function Test-Admin
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
if (-not (Test-Admin))
{
    $arg = "-NoProfile -ExecutionPolicy Bypass -File `"" + $PSCommandPath + "`""
    Start-Process -FilePath "powershell.exe" -ArgumentList $arg -Verb RunAs -Wait | Out-Null
    exit 0
}
$dest = Split-Path -Parent $PSCommandPath
$keyName = (Get-Content -LiteralPath (Join-Path $dest "uninstall-key.txt") -Encoding ASCII).Trim()
$display = (Get-Content -LiteralPath (Join-Path $dest "uninstall-name.txt") -Encoding UTF8).Trim()
Remove-Item -LiteralPath "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$keyName" -Recurse -Force -ErrorAction SilentlyContinue
$start = Join-Path $env:ProgramData "Microsoft\Windows\Start Menu\Programs\$display.lnk"
Remove-Item -LiteralPath $start -Force -ErrorAction SilentlyContinue
Set-Location $env:SystemRoot
Start-Sleep -Milliseconds 400
Remove-Item -LiteralPath $dest -Recurse -Force
'@ | Set-Content -LiteralPath $uninstall -Encoding UTF8

Set-Content -LiteralPath (Join-Path $dest 'uninstall-key.txt') -Value $keyName -Encoding ASCII
Set-Content -LiteralPath (Join-Path $dest 'uninstall-name.txt') -Value $display -Encoding UTF8

$programs = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'
New-Item -ItemType Directory -Force -Path $programs | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path $programs ($display + '.lnk')))
$shortcut.TargetPath = Join-Path $dest 'Unique.exe'
$shortcut.WorkingDirectory = $dest
$shortcut.Description = $display
$shortcut.Save()

$uninstallCmd = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $uninstall + '"'
$regPath = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$keyName"
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'DisplayName' -Value $display -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'DisplayVersion' -Value $version -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'Publisher' -Value 'e-face' -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'InstallLocation' -Value $dest -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'DisplayIcon' -Value (Join-Path $dest 'Unique.exe') -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'UninstallString' -Value $uninstallCmd -PropertyType String -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'NoModify' -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $regPath -Name 'NoRepair' -Value 1 -PropertyType DWord -Force | Out-Null

Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.MessageBox]::Show(
    "Unique $version установлен в`n$dest",
    $display,
    [System.Windows.Forms.MessageBoxButtons]::OK,
    [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
