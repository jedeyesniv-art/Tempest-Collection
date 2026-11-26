param([switch]$Live,[int]$PresentHz=0,[string]$Binary="")
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$runs=Join-Path (Split-Path $project -Parent) 'tempest-profile'
$image=Join-Path $runs 'runs/Tempest3000-reconstructed.iso'
$binary=if($Binary){(Resolve-Path $Binary).Path}else{Join-Path $project 'build-native/Release/t3k.exe'}
$cases=@(
 @{Name='level1';Level=0;Pattern=0;Scenario='--check-combat';Limit=6000},
 @{Name='level9';Level=8;Pattern=3;Scenario='--check-combat';Limit=9000},
 @{Name='analog';Level=14;Pattern=4;Scenario='--check-combat';Limit=3600},
 @{Name='mouse';Level=0;Pattern=3;Scenario='--check-mouse';Limit=2400},
 @{Name='two-player';Level=0;Pattern=0;Scenario='--check-two-player';Limit=1800},
 @{Name='level-select';Level=0;Pattern=0;Scenario='--check-level';Limit=1500},
 @{Name='warp';Level=0;Pattern=0;Scenario='--check-warp';Limit=0},
 @{Name='tools';Level=0;Pattern=0;Scenario='--check-tools';Limit=0},
 @{Name='password';Level=0;Pattern=0;Scenario='--check-password';Limit=0}
)
if($Live){$cases=@(@{Name='live';Level=0;Pattern=0;Scenario='--live-check';Limit=0})}
$oldLevel=$env:TEMPEST_CHECK_LEVEL;$oldPattern=$env:TEMPEST_CHECK_PATTERN
try{
 foreach($case in $cases){
  $env:TEMPEST_CHECK_LEVEL=[string]$case.Level;$env:TEMPEST_CHECK_PATTERN=[string]$case.Pattern
  $name='all-scene-'+$PresentHz+'-'+$case.Name
  $userdata=Join-Path $runs ('runs/'+$name)
  New-Item -ItemType Directory -Force -Path $userdata | Out-Null
  $arguments='"'+$image+'" --logic-hz=120 '+$case.Scenario+' --fine-check-limit='+$case.Limit+' --winsize=720x480 --novsync --userdata="'+$userdata+'" --set=crt=0 --set=native_interpolate=0 --set=game_speed=24 --set=native_menu_speed=24'
  if(!$Live){$arguments+=' --timing-trace="'+(Join-Path $userdata 'trace')+'"'}
  if($PresentHz){$arguments+=' --present-hz='+$PresentHz}
  $stdout=Join-Path $runs ($name+'.log');$stderr=Join-Path $runs ($name+'.err')
  $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $log=(Get-Content $stdout -Raw)+(Get-Content $stderr -Raw)
  if($process.ExitCode -ne 0 -or $log -match 'Native flow stopped|runtime_error|Invalid simulation|Native runtime:|validation: FAILED'){throw ($name+': '+$log)}
  $marker=if($Live){'Live clock/audio check:'}elseif($case.Name -eq 'tools'){'Native original rotary calibration: passed'}elseif($case.Name -eq 'password'){'Native password validation: passed'}elseif($case.Name -eq 'warp'){'Native warp-game validation: passed'}else{'bounded soak|Native original name entry'}
  if($log -notmatch $marker){throw ($name+': missing completion marker')}
  Write-Output ($name+': passed');Get-Content $stdout -Tail 4
 }
}finally{$env:TEMPEST_CHECK_LEVEL=$oldLevel;$env:TEMPEST_CHECK_PATTERN=$oldPattern}
