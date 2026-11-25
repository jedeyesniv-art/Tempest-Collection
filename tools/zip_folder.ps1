# Write-ZipFolder <folder> <zip>: zips the folder (as the archive's top folder) entry by entry.
# Windows PowerShell 5.1's Compress-Archive names entries with backslashes, which Linux unzips as files called
# "TempestCollection\licences\SOURCE.txt" (bug report 2026-11-25; Minotaur had it too). Forward slashes and a folder
# entry for each folder, as the zip spec has it. Same code as C:\Gridios\tools\make-package.ps1.
function Write-ZipFolder([string]$Folder, [string]$Zip) {
    if (Test-Path $Zip) { Remove-Item -Force $Zip }
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $folderItem = Get-Item $Folder
    $root = $folderItem.Parent.FullName.TrimEnd('\')
    $archive = [IO.Compression.ZipFile]::Open($Zip, 'Create')
    try {
        $items = @($folderItem) + @(Get-ChildItem $folderItem.FullName -Recurse -Force | Sort-Object FullName)
        foreach ($item in $items) {
            $name = $item.FullName.Substring($root.Length + 1).Replace('\', '/')
            if ($item.PSIsContainer) {
                [void]$archive.CreateEntry("$name/")
            } else {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $item.FullName, $name, 'Optimal')
            }
        }
    } finally {
        $archive.Dispose()
    }
    # Check: no entry may hold a backslash.
    $check = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $badNames = @($check.Entries | Where-Object { $_.FullName.Contains('\') })
        if ($badNames.Count) { Write-Error "zip entries with backslashes: $($badNames[0].FullName)" }
    } finally {
        $check.Dispose()
    }
}
