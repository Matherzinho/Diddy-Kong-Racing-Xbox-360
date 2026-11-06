# dkr360: runs dkr360.xex in a Xenia Canary copy that sits in the same folder (portable.txt beside it), for Seconds,
# then prints the end of dkr360.log and xenia.log's crash lines. See tools/xenia/README.md.
param([int]$Seconds = 40, [int]$Tail = 60, [string]$Folder = "E:\oot360-dev\Diddy Kong Racing 360\xenia-teste")
$d = $Folder
Get-Process xenia_canary -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$d*" } | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item "$d\xenia.log", "$d\dkr360.log" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$d\xenia_canary.exe" -ArgumentList '--allow_game_relative_writes=true', '--readback_resolve=full', "`"$d\dkr360.xex`"" -WorkingDirectory $d -PassThru
$t = 0
while ($t -lt $Seconds -and -not $p.HasExited) { Start-Sleep 1; $t++ }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
"ran $t s, exited: $($p.HasExited)"
if (Test-Path "$d\dkr360.log") { Get-Content "$d\dkr360.log" -Tail $Tail }
if (Test-Path "$d\xenia.log") { Select-String -Path "$d\xenia.log" -Pattern "CRASH|Unimplemented|PC:|assert" | Select-Object -First 20 | ForEach-Object { $_.Line } }
