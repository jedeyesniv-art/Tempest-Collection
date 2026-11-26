param([string]$Binary="build-native/Release/t3k-ux.exe")
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$runs=Join-Path (Split-Path $project -Parent) 'tempest-profile'
$image=Join-Path $runs 'runs/Tempest3000-reconstructed.iso'
$binary=(Resolve-Path $Binary).Path
$cases=@(
 @{Name='off-default';High=0;Custom=$false;Scenario='--check-combat';Limit=1200},
 @{Name='off-custom';High=0;Custom=$true;Scenario='--check-combat';Limit=600},
 @{Name='on-custom';High=1;Custom=$true;Scenario='--check-combat';Limit=1200},
 @{Name='off-warp';High=0;Custom=$false;Scenario='--check-warp';Limit=0},
 @{Name='off-tools';High=0;Custom=$true;Scenario='--check-tools';Limit=0},
 @{Name='off-password';High=0;Custom=$true;Scenario='--check-password';Limit=0}
)
$oldLevel=$env:TEMPEST_CHECK_LEVEL;$oldPattern=$env:TEMPEST_CHECK_PATTERN
try{
 foreach($case in $cases){
  $env:TEMPEST_CHECK_LEVEL='8';$env:TEMPEST_CHECK_PATTERN='3'
  $name='timing-ux-'+$case.Name;$userdata=Join-Path $runs ('runs/'+$name)
  New-Item -ItemType Directory -Force -Path $userdata | Out-Null
  $arguments='"'+$image+'" --logic-hz=120 '+$case.Scenario+' --fine-check-limit='+$case.Limit+' --winsize=720x480 --novsync --userdata="'+$userdata+'" --set=crt=0 --set=native_interpolate=0 --set=native_high_framerate='+$case.High+' --timing-trace="'+(Join-Path $userdata 'trace')+'"'
  if($case.Custom){$arguments+=' --set=native_gameplay_speed=25 --set=native_opening_speed=27 --set=native_menu_speed=29 --set=native_travel_speed=19 --set=native_clear_speed=47 --set=native_bonus_speed=37 --set=native_death_speed=23 --set=native_tool_speed=31 --set=native_name_speed=33'}
  $stdout=Join-Path $runs ($name+'.log');$stderr=Join-Path $runs ($name+'.err')
  $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $log=(Get-Content $stdout -Raw)+(Get-Content $stderr -Raw)
  if($process.ExitCode -ne 0 -or $log -match 'Native flow stopped|runtime_error|Invalid simulation|Native runtime:|validation: FAILED'){throw ($name+': '+$log)}
  if($log -notmatch 'bounded soak|Native original name entry|Native warp-game validation: passed|Native original rotary calibration: passed|Native password validation: passed'){throw ($name+': missing completion marker')}
  $frames=Import-Csv (Join-Path $userdata 'trace.frames.csv')
  if($case.High -eq 0){
   if($frames | Where-Object {[int]$_.divisions -ne 1 -or [int]$_.step_numerator -ne 1 -or [int]$_.sample_hz -ne [int]$_.reference_hz}){throw ($name+': Off did not use complete reference ticks')}
  }elseif($frames | Where-Object {[int]$_.sample_hz -ne 120}){throw ($name+': high-rate sample cadence drifted')}
  for($i=0;$i -lt $frames.Count;++$i){if([int]$frames[$i].host_frame -ne $i -or !$frames[$i].sample_hz){throw ($name+': incomplete timing evidence')}}
  if($log -match '(\d+) total pictures' -and $frames.Count -ne [int]$Matches[1]){throw ($name+': timing trace is incomplete')}
  $opening=@($frames | Where-Object {[int]$_.scene_kind -eq 1})
  $expected=if($case.Scenario -in @('--check-tools','--check-password')){0}elseif($case.High -eq 0){126}elseif($case.Custom){560}else{630}
  if($opening.Count -ne $expected){throw ($name+': opening duration '+$opening.Count+' expected '+$expected)}
  Write-Output ($name+': passed, '+$frames.Count+' pictures; opening='+$opening.Count)
 }
}finally{$env:TEMPEST_CHECK_LEVEL=$oldLevel;$env:TEMPEST_CHECK_PATTERN=$oldPattern}
