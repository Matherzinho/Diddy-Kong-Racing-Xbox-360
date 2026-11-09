param([int]$Seconds=100,[int]$Skip=38,[int]$First=40,[string]$Xex="\\wsl.localhost\Debian\home\Matherzin\dkr360-work\build\dkr360.xex",[string]$Filter="")
$f="E:\oot360-dev\Diddy Kong Racing 360\xenia-teste"
Copy-Item $Xex "$f\dkr360.xex" -Force
Remove-Item "$f\dkr360_frame_*.ppm" -ea 0
$p=Start-Process -FilePath "$f\xenia_canary.exe" -ArgumentList '--allow_game_relative_writes=true','--readback_resolve=full',"`"$f\dkr360.xex`"" -WorkingDirectory $f -PassThru
Start-Sleep $Seconds; if(-not $p.HasExited){Stop-Process -Id $p.Id -Force}
if ($Filter -ne "") { Get-Content "$f\dkr360.log" | Select-String $Filter | % Line }
else { Get-Content "$f\dkr360.log" | Select-Object -Skip $Skip -First $First }
(Get-ChildItem $f\*.ppm | % Name) -join ' '
