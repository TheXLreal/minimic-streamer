param(
    [string]$ToolchainBin = $env:ARM_GCC_BIN,
    [string]$Ninja = '',
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$repoDir = Split-Path -Parent $PSScriptRoot
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}
Push-Location $repoDir
try {
    if (!$ToolchainBin) {
        $gccCommand = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
        if ($gccCommand) { $ToolchainBin = Split-Path $gccCommand.Source }
        else {
            $gccFile = Get-ChildItem "$env:USERPROFILE\.pico-sdk\toolchain\*\bin\arm-none-eabi-gcc.exe" -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
            if ($gccFile) { $ToolchainBin = $gccFile.DirectoryName }
        }
    }
    if (!$ToolchainBin -or !(Test-Path -LiteralPath "$ToolchainBin/arm-none-eabi-gcc.exe")) {
        throw 'Install Arm GNU Toolchain and pass -ToolchainBin <directory containing arm-none-eabi-gcc.exe>.'
    }
    if (!$Ninja) {
        $ninjaCommand = Get-Command ninja -ErrorAction SilentlyContinue
        if ($ninjaCommand) { $Ninja = $ninjaCommand.Source }
        else {
            $ninjaFile = Get-ChildItem "$env:USERPROFILE\.pico-sdk\ninja\*\ninja.exe" -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending | Select-Object -First 1
            if ($ninjaFile) { $Ninja = $ninjaFile.FullName }
        }
    }
    if (!$Ninja) { throw 'Install Ninja or pass -Ninja <path to ninja.exe>.' }
    $deps = @(
        @{Name='tinyusb'; Url='https://github.com/hathach/tinyusb.git'; Tag='0.20.0'; Commit='3af1bec1a9161ee8dec29487831f7ac7ade9e189'},
        @{Name='nrfx'; Url='https://github.com/NordicSemiconductor/nrfx.git'; Tag='v3.14.0'; Commit='11f57e578c7feea13f21c79ea0efab2630ac68c7'},
        @{Name='cmsis'; Url='https://github.com/ARM-software/CMSIS_5.git'; Tag='5.9.0'; Commit='2b7495b8535bdcb306dac29b9ded4cfb679d7e5c'}
    )
    New-Item -ItemType Directory -Force third_party | Out-Null
    foreach ($dep in $deps) {
        $depDir = Join-Path $repoDir "third_party/$($dep.Name)"
        if (!(Test-Path -LiteralPath $depDir)) {
            Invoke-Checked git @('clone','--depth','1','--branch',$dep.Tag,'--filter=blob:none','--sparse',$dep.Url,$depDir)
            if ($dep.Name -eq 'cmsis') { Invoke-Checked git @('-C',$depDir,'sparse-checkout','set','CMSIS/Core/Include') }
            else { Invoke-Checked git @('-C',$depDir,'sparse-checkout','disable') }
        }
        $actual = & git -C $depDir rev-parse HEAD
        if ($LASTEXITCODE -ne 0 -or $actual.Trim() -ne $dep.Commit) { throw "Wrong dependency revision: $depDir" }
        $changes = & git -C $depDir status --porcelain --untracked-files=no
        if ($changes) { throw "Modified dependency: $depDir. Restore it or use a clean checkout." }
    }
    Invoke-Checked cmake @('-S','.', '-B','build/nrf5340dk','-G','Ninja',
        '-DCMAKE_SYSTEM_NAME=Generic','-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY',
        "-DCMAKE_C_COMPILER=$ToolchainBin/arm-none-eabi-gcc.exe",
        "-DCMAKE_ASM_COMPILER=$ToolchainBin/arm-none-eabi-gcc.exe",
        "-DCMAKE_OBJCOPY=$ToolchainBin/arm-none-eabi-objcopy.exe", "-DCMAKE_MAKE_PROGRAM=$Ninja")
    Invoke-Checked cmake @('--build','build/nrf5340dk','--parallel')
    Invoke-Checked $Python @('tools/package_firmware.py','--compiler',"$ToolchainBin/arm-none-eabi-gcc.exe")
} finally { Pop-Location }
