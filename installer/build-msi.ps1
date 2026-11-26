# Builds Unique-0.4.9-win32.msi and Unique-0.4.9-win64.msi from dist/stage-win32 and dist/stage-win64.
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$wix = Join-Path $repo 'build\wix-tool\wix.exe'
if (-not (Test-Path -LiteralPath $wix))
{
    throw "WiX не найден: $wix"
}

function ConvertTo-MsiId([string] $relativePath)
{
    $id = ($relativePath -replace '[^A-Za-z0-9_.]', '_')
    if ($id -notmatch '^[A-Za-z_]')
    {
        $id = '_' + $id
    }
    if ($id.Length -gt 68)
    {
        $md5 = [System.Security.Cryptography.MD5]::Create()
        $hash = ($md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($relativePath.ToLowerInvariant())) | ForEach-Object { $_.ToString('x2') }) -join ''
        $id = $id.Substring(0, 40) + '_' + $hash.Substring(0, 12)
    }
    return $id
}

function New-StableGuid([string] $seed)
{
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $bytes = $md5.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($seed))
    $bytes[7] = (($bytes[7] -band 0x0F) -bor 0x30)
    $bytes[8] = (($bytes[8] -band 0x3F) -bor 0x80)
    return (New-Object System.Guid (, $bytes)).ToString().ToUpperInvariant()
}

function Escape-Xml([string] $value)
{
    return $value.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;').Replace('"', '&quot;')
}

function Write-FileList([string] $stage, [string] $archId, [string] $outFile)
{
    if (Test-Path -LiteralPath (Join-Path $stage 'config\settings.ini'))
    {
        throw "В поставке есть settings.ini: $stage"
    }
    if (Test-Path -LiteralPath (Join-Path $stage 'config\ai'))
    {
        throw "В поставке есть config\ai: $stage"
    }

    $root = (Resolve-Path -LiteralPath $stage).Path.TrimEnd('\')
    $files = @(Get-ChildItem -LiteralPath $root -Recurse -File)
    if ($files.Count -eq 0)
    {
        throw "Пустая поставка: $stage"
    }

    $byDir = @{}
    $dirSet = New-Object System.Collections.Generic.List[string]
    foreach ($file in $files)
    {
        $rel = $file.FullName.Substring($root.Length).TrimStart('\')
        $dir = Split-Path -Parent $rel
        if ($null -eq $dir)
        {
            $dir = ''
        }
        if (-not $byDir.ContainsKey($dir))
        {
            $byDir[$dir] = New-Object System.Collections.Generic.List[System.IO.FileInfo]
            $walk = $dir
            while ($walk)
            {
                if (-not $dirSet.Contains($walk))
                {
                    $dirSet.Add($walk)
                }
                $parent = Split-Path -Parent $walk
                if ($parent -eq $walk)
                {
                    break
                }
                $walk = $parent
            }
        }
        $byDir[$dir].Add($file)
    }

    $ids = @{}
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine('<Include xmlns="http://wixtoolset.org/schemas/v4/wxs">')
    [void]$sb.AppendLine('<Fragment>')
    [void]$sb.AppendLine('    <DirectoryRef Id="INSTALLFOLDER">')

    function Emit-Level([string] $dir, [int] $depth)
    {
        $pad = '    ' + ('    ' * $depth)
        $children = @($dirSet | Where-Object {
            if ($dir -eq '')
            {
                return (($_ -split '\\').Count -eq 1)
            }
            $parent = Split-Path -Parent $_
            if ($null -eq $parent)
            {
                $parent = ''
            }
            return ($parent -eq $dir)
        } | Sort-Object)
        foreach ($child in $children)
        {
            $name = Split-Path -Leaf $child
            $dirId = 'dir_' + (ConvertTo-MsiId $child)
            if ($ids.ContainsKey($dirId))
            {
                throw "Повторяется идентификатор каталога: $dirId"
            }
            $ids[$dirId] = $true
            [void]$sb.AppendLine("$pad<Directory Id=`"$dirId`" Name=`"$(Escape-Xml $name)`">")
            Emit-Level $child ($depth + 1)
            [void]$sb.AppendLine("$pad</Directory>")
        }
        if ($byDir.ContainsKey($dir))
        {
            foreach ($file in ($byDir[$dir] | Sort-Object Name))
            {
                $rel = $file.FullName.Substring($root.Length).TrimStart('\')
                $baseId = ConvertTo-MsiId $rel
                $cmpId = 'cmp_' + $baseId
                $filId = 'fil_' + $baseId
                if ($cmpId.Length -gt 72 -or $filId.Length -gt 72)
                {
                    throw "Слишком длинный идентификатор: $rel"
                }
                if ($ids.ContainsKey($cmpId))
                {
                    throw "Повторяется компонент: $cmpId"
                }
                $ids[$cmpId] = $rel
                $source = $file.FullName.Replace('\', '/')
                $guid = New-StableGuid ("Unique-0.1.1-$archId|$rel")
                [void]$sb.AppendLine("$pad<Component Id=`"$cmpId`" Guid=`"$guid`">")
                [void]$sb.AppendLine("$pad    <File Id=`"$filId`" Source=`"$(Escape-Xml $source)`" KeyPath=`"yes`" />")
                [void]$sb.AppendLine("$pad</Component>")
            }
        }
    }

    Emit-Level '' 1
    [void]$sb.AppendLine('    </DirectoryRef>')
    [void]$sb.AppendLine('</Fragment>')
    [void]$sb.AppendLine('<Fragment>')
    [void]$sb.AppendLine('    <ComponentGroup Id="AppFiles">')
    foreach ($cmpId in ($ids.Keys | Where-Object { $_.StartsWith('cmp_') } | Sort-Object))
    {
        [void]$sb.AppendLine("        <ComponentRef Id=`"$cmpId`" />")
    }
    [void]$sb.AppendLine('    </ComponentGroup>')
    [void]$sb.AppendLine('</Fragment>')
    [void]$sb.AppendLine('</Include>')

    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($outFile, $sb.ToString(), $utf8)
}

$outDir = Join-Path $repo 'build\wix'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$builds = @(
    @{
        Arch = 'x86'
        ArchId = 'x86'
        Label = '32-bit'
        ProgramFilesId = 'ProgramFilesFolder'
        Stage = Join-Path $repo 'dist\stage-win32'
        UpgradeCode = '7C4E9A2B-1F6D-4C8A-9E3B-5A1D8F0C2E64'
        ShortcutGuid = '3B8E1D54-6A27-4F9C-8D10-C5E7A9B24F31'
        Out = Join-Path $repo 'dist\Unique-0.4.9-win32.msi'
    },
    @{
        Arch = 'x64'
        ArchId = 'x64'
        Label = '64-bit'
        ProgramFilesId = 'ProgramFiles64Folder'
        Stage = Join-Path $repo 'dist\stage-win64'
        UpgradeCode = '9E2A6C48-5B71-4D0F-A3E8-1C7F5D9B60A2'
        ShortcutGuid = '6F1C4A90-8D32-4E5B-B7A1-2D8E0C5F93B6'
        Out = Join-Path $repo 'dist\Unique-0.4.9-win64.msi'
    }
)

foreach ($build in $builds)
{
    $wxi = Join-Path $outDir ("files-" + $build.ArchId + '.wxi')
    Write-FileList $build.Stage $build.ArchId $wxi
    & $wix build -arch $build.Arch -pdbtype none `
        -d ("ArchLabel=" + $build.Label) `
        -d ("ArchId=" + $build.ArchId) `
        -d ("UpgradeCode=" + $build.UpgradeCode) `
        -d ("ShortcutGuid=" + $build.ShortcutGuid) `
        -d ("ProgramFilesId=" + $build.ProgramFilesId) `
        -d ("Repo=" + ($repo -replace '\\', '/')) `
        -d ("FilesWxi=" + ($wxi -replace '\\', '/')) `
        -o $build.Out `
        (Join-Path $PSScriptRoot 'unique.wxs')
    if ($LASTEXITCODE -ne 0)
    {
        throw "Сборка MSI не удалась: $($build.Out)"
    }
    Write-Output $build.Out
}
