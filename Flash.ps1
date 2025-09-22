$COMPort = "COM4"
$ESPToolPath = Get-ChildItem "$env:HOMEPATH\Documents\Software\esptool\esptool-*\esptool-win64\esptool.exe" | Select-Object -First 1
& $ESPToolPath -p $ComPort erase_flash
& $ESPToolPath -p $COMPort write_flash -fm dout 0x1000 .vscode/output/yoRadio.ino.bootloader.bin
& $ESPToolPath -p $COMPort write_flash -fm dout 0x8000 .vscode/output/yoRadio.ino.partitions.bin
& $ESPToolPath -p $COMPort write_flash -fm dout 0x10000 .vscode/output/yoRadio.ino.bin
