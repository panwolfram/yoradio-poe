$CurrentDir = $PWD.Path
$StartDir = "$($PWD.Path)\data\www"

Set-Location $StartDir; Get-ChildItem | ? Attributes -EQ Directory | % {
  Set-Location $_.FullName
  Get-Item "$($_.BaseName).gz" | Remove-Item
  & 'C:\Program Files\7-Zip\7z.exe' a "$($_.BaseName).gz" $_.BaseName
  Move-Item -Force "$($_.BaseName).gz" ..\
}

Set-Location $CurrentDir