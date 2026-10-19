param(
    [string]$Port = '',
    [ValidateSet('STATUS','ON','OFF','MUTE','UNMUTE','PAIR','CANCEL','REBOOT','HELP','MONITOR ON','MONITOR OFF')]
    [string]$Command = 'STATUS',
    [ValidateRange(0,20000)][Nullable[uint32]]$PairDelayUs = $null,
    [switch]$Watch
)
$ErrorActionPreference = 'Stop'
if (!$Port) {
    Write-Host 'Choose the COM port for MiniMic Control (USB VID CAFE, PID 5340, interface 02), not the J-Link UART.'
    Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_CAFE&PID_5340' } |
        Select-Object Name,PNPDeviceID | Format-List
    Write-Host 'Example: .\tools\control-nrf5340.ps1 -Port COM7 -Command PAIR'
    return
}
if ($Port -notmatch '^COM[0-9]+$') { throw 'Specify a port such as COM7.' }
$serial = [System.IO.Ports.SerialPort]::new($Port,115200,'None',8,'One')
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
try {
    $serial.Open()
    if ($null -ne $PairDelayUs) {
        if ($PairDelayUs -gt 0 -and $PairDelayUs -lt 1000) { throw 'PairDelayUs must be 0 (auto) or 1000..20000.' }
        $serial.WriteLine("PAIRDELAY $PairDelayUs")
    }
    $serial.WriteLine($Command)
    $stopAt = [DateTime]::UtcNow.AddSeconds(3)
    Write-Host "$Command sent to $Port. Ctrl+C stops watching."
    do {
        try { $line = $serial.ReadLine(); Write-Host $line.TrimEnd() }
        catch [System.TimeoutException] { }
    } while ($Watch -or [DateTime]::UtcNow -lt $stopAt)
} finally { if ($serial.IsOpen) { $serial.Close() }; $serial.Dispose() }
