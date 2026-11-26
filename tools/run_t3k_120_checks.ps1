param([switch]$Live,[int]$PresentHz=0)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$runs=Join-Path (Split-Path $project -Parent) 'tempest-profile'
$image=Join-Path $runs 'runs/Tempest3000-reconstructed.iso'
$binary=Join-Path $project 'build-native/Release/t3k.exe'
$cases=@(
    @{Name='level1-digital';Level=0;Pattern=0;Scenario='--check-combat';Limit=2400},
    @{Name='level9-zapper';Level=8;Pattern=3;Scenario='--check-combat';Limit=3600},
    @{Name='level15-analog';Level=14;Pattern=4;Scenario='--check-combat';Limit=3600},
    @{Name='mouse';Level=0;Pattern=3;Scenario='--check-mouse';Limit=2400}
)
if($Live){$cases=@(@{Name=$(if($PresentHz){'live-clock-'+$PresentHz}else{'live-clock'});Level=0;Pattern=0;Scenario='--live-check';Limit=0})}
$previousLevel=$env:TEMPEST_CHECK_LEVEL
$previousPattern=$env:TEMPEST_CHECK_PATTERN
$jobs=@()
try{
    foreach($case in $cases){
        $env:TEMPEST_CHECK_LEVEL=[string]$case.Level
        $env:TEMPEST_CHECK_PATTERN=[string]$case.Pattern
        $userdata=Join-Path $runs ('runs/timing-120-'+$case.Name)
        $arguments='"'+$image+'" --logic-hz=120 '+$case.Scenario+' --fine-check-limit='+$case.Limit+' --winsize=720x480 --userdata="'+$userdata+'" --set=native_interpolate=0 --set=crt=0'
        if($PresentHz){$arguments+=' --present-hz='+$PresentHz}
        if($case.Name -eq 'level9-zapper'){
            $trace=Join-Path $runs 'runs/timing-research/validated-120'
            $shot=Join-Path $runs 'runs/timing-research/120-level9.bmp'
            $arguments+=' --timing-trace="'+$trace+'" --winshot=1200:"'+$shot+'"'
        }
        $stdout=Join-Path $runs ('timing-120-'+$case.Name+'.log')
        $stderr=Join-Path $runs ('timing-120-'+$case.Name+'.err')
        $process=Start-Process -FilePath $binary -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $jobs+=@{Name=$case.Name;Process=$process;Stdout=$stdout;Stderr=$stderr}
    }
}finally{
    $env:TEMPEST_CHECK_LEVEL=$previousLevel
    $env:TEMPEST_CHECK_PATTERN=$previousPattern
}
$failures=0
foreach($job in $jobs){
    $job.Process.WaitForExit()
    $output=Get-Content -LiteralPath $job.Stdout -Raw
    $errors=Get-Content -LiteralPath $job.Stderr -Raw
    $passed=$job.Process.ExitCode -eq 0 -and $output -match '120 Hz bounded soak:|Native original name entry|Live clock/audio check:' -and $errors -notmatch 'Native flow stopped|Native runtime:'
    if(!$passed){++$failures}
    Write-Output ($job.Name+': '+$(if($passed){'passed'}else{'FAILED'})+' (exit '+$job.Process.ExitCode+')')
    Write-Output $output.Trim()
    if(!$passed){Write-Output $errors.Trim()}
}
exit $failures
