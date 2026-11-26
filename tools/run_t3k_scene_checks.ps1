param([switch]$Live,[int]$MenuHz=24,[int]$WarpHz=60,[int]$PresentHz=0,[string]$Binary="")
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$runs=Join-Path (Split-Path $project -Parent) 'tempest-profile'
$image=Join-Path $runs 'runs/Tempest3000-reconstructed.iso'
$binary=if($Binary){(Resolve-Path $Binary).Path}else{Join-Path $project 'build-native/Release/t3k.exe'}
$cases=@(
    @{Name='level9';Level=8;Pattern=3;Scenario='--check-combat';Limit=9000},
    @{Name='analog';Level=14;Pattern=4;Scenario='--check-combat';Limit=3600},
    @{Name='mouse';Level=0;Pattern=3;Scenario='--check-mouse';Limit=2400}
)
if($Live){$cases=@(@{Name='live';Level=0;Pattern=0;Scenario='--live-check';Limit=0})}
$previousLevel=$env:TEMPEST_CHECK_LEVEL
$previousPattern=$env:TEMPEST_CHECK_PATTERN
try{
    foreach($case in $cases){
        $env:TEMPEST_CHECK_LEVEL=[string]$case.Level
        $env:TEMPEST_CHECK_PATTERN=[string]$case.Pattern
        $name='scene-'+$MenuHz+'-'+$WarpHz+'-'+$PresentHz+'-'+$case.Name
        $userdata=Join-Path $runs ('runs/'+$name)
        New-Item -ItemType Directory -Force -Path $userdata | Out-Null
        $trace=Join-Path $userdata 'trace'
        $arguments='"'+$image+'" --logic-hz=120 --scene-hz=120 '+$case.Scenario+' --fine-check-limit='+$case.Limit+' --winsize=720x480 --novsync --userdata="'+$userdata+'" --set=crt=0 --set=native_interpolate=0 --set=game_speed=24 --set=native_menu_speed='+$MenuHz+' --set=warp_speed='+$WarpHz+' --set=warp_ramp=0'
        if(!$Live){$arguments+=' --timing-trace="'+$trace+'"'}
        if($PresentHz){$arguments+=' --present-hz='+$PresentHz}
        $stdout=Join-Path $runs ($name+'.log');$stderr=Join-Path $runs ($name+'.err')
        $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $log=(Get-Content $stdout -Raw)+(Get-Content $stderr -Raw)
        if($process.ExitCode -ne 0 -or $log -match 'Native flow stopped|runtime_error|Invalid simulation|native .* failed'){throw ($name+': '+$log)}
        if($Live -and $log -notmatch 'Live clock/audio check:'){throw ($name+': missing live completion')}
        if(!$Live -and $log -notmatch 'bounded soak|Native original name entry'){throw ($name+': missing completion')}
        Write-Output ($name+': passed')
        Get-Content $stdout -Tail 4
    }
}finally{
    $env:TEMPEST_CHECK_LEVEL=$previousLevel
    $env:TEMPEST_CHECK_PATTERN=$previousPattern
}
