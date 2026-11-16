param(
    [string]$Port = 'COM25',
    [switch]$CheckCard,
    [switch]$CheckCardWrite,
    [string]$Log
)
$ErrorActionPreference = 'Stop'
$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, 'None', 8, 'One')
$serial.Handshake = 'None'
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.WriteTimeout = 1000
$transcript = [System.Text.StringBuilder]::new()

function Receive-Until([string]$Expected, [int]$Timeout = 3000) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $data = [System.Text.StringBuilder]::new()
    do {
        $received = $serial.ReadExisting()
        [void] $data.Append($received)
        [void] $transcript.Append($received)
        if ($data.ToString().Contains($Expected)) { return $data.ToString() }
        Start-Sleep -Milliseconds 20
    } while ($watch.ElapsedMilliseconds -lt $Timeout)
    throw "Expected serial text '$Expected', received '$data'"
}
function Drain([int]$Milliseconds = 200) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $data = [System.Text.StringBuilder]::new()
    do {
        $received = $serial.ReadExisting()
        [void] $data.Append($received)
        [void] $transcript.Append($received)
        Start-Sleep -Milliseconds 20
    } while ($watch.ElapsedMilliseconds -lt $Milliseconds)
    return $data.ToString()
}
function Submit([string]$Line, [string]$Expected, [int]$Timeout = 3000) {
    $serial.Write($Line + "`r")
    [void] (Receive-Until $Expected $Timeout)
    [void] (Drain)
}
try {
    $serial.Open()
    $esc = [char]27
    $serial.Write("`r")
    [void] (Receive-Until "BASIC> " 5000)
    [void] (Drain)
    Submit 'HELP' 'More help: HELP BASIC'
    Submit 'HELP EDIT' 'Unsaved edits stay in RAM.'
    Submit 'HELP SD' 'Unmount before removing the card.'
    Submit ('BASIC PRINT 4+12' + $esc + '[D' + $esc + '[3~') "`r`n5`r`nBASIC> "
    Submit ('BASIC PRINT 42x' + [char]127) "`r`n42`r`nBASIC> "
    Submit 'BASIC PRINT 1.25+2.5' "`r`n3.75`r`nBASIC> "
    Submit 'BASIC PRINT UCASE$("nucleo")+"!"' "`r`nNUCLEO!`r`nBASIC> "
    Submit 'NEW' "`r`nBASIC> "
    Submit 'EDIT' 'Structured BASIC editor'
    $serial.Write(('PRINT "EDITOR OK"x' + [char]127 + "`r" + [char]17))
    [void] (Receive-Until 'Changes held in RAM.')
    [void] (Drain)
    Submit 'RUN' "`r`nEDITOR OK`r`nBASIC> "
    $serial.Write("NEW`r")
    [void] (Receive-Until 'Discard unsaved buffer?')
    Submit 'y' "`r`nBASIC> "
    Submit 'APPEND INPUT N' "`r`nBASIC> "
    Submit 'APPEND PRINT N*2' "`r`nBASIC> "
    $serial.Write("RUN`r")
    [void] (Receive-Until '? ')
    Submit ('20' + [char]127 + '1') "`r`n42`r`nBASIC> "
    $serial.Write("NEW`r")
    [void] (Receive-Until 'Discard unsaved buffer?')
    Submit 'y' "`r`nBASIC> "
    Submit 'APPEND WHILE 1' "`r`nBASIC> "
    Submit 'APPEND WEND' "`r`nBASIC> "
    $serial.Write("RUN`r")
    Start-Sleep -Milliseconds 150
    $serial.Write([string][char]3)
    [void] (Receive-Until 'Execution interrupted')
    [void] (Drain)
    Submit 'BASIC PRINT 9' "`r`n9`r`nBASIC> "
    $serial.Write("NEW`r")
    [void] (Receive-Until 'Discard unsaved buffer?')
    Submit 'y' "`r`nBASIC> "
    Submit 'SDCLK' 'SDIO CLK = PC12.'
    if ($CheckCard -or $CheckCardWrite) {
        $mounted = $false
        $serial.Write("MOUNT`r")
        $result = (Receive-Until "`r`nBASIC> " 14000)
        if ($result.Contains('SD mounted.')) {
            $mounted = $true
            Submit 'PWD' "`r`n/`r`nBASIC> "
            $serial.Write("DIR`r")
            [void] (Receive-Until "`r`nBASIC> " 14000)
            Write-Output 'SD: PC12 mount and root listing PASS'
        } else {
            Write-Output ("SD: PC12 " + (($result -split "`r?`n") | Where-Object { $_ -like 'SD:*' }))
            Submit 'SDCLK PB15' 'SDIO CLK = PB15.'
            $serial.Write("MOUNT`r")
            $result = (Receive-Until "`r`nBASIC> " 14000)
            if ($result.Contains('SD mounted.')) {
                $mounted = $true
                Submit 'PWD' "`r`n/`r`nBASIC> "
                $serial.Write("DIR`r")
                [void] (Receive-Until "`r`nBASIC> " 14000)
                Write-Output 'SD: PB15 mount and root listing PASS'
            } else {
                Write-Output ("SD: PB15 " + (($result -split "`r?`n") | Where-Object { $_ -like 'SD:*' }))
            }
            if (!$mounted) { Submit 'SDCLK PC12' 'SDIO CLK = PC12.' }
        }
        if ($CheckCardWrite) {
            if (!$mounted) { throw 'Card did not mount for write test' }
            $name = '/sbasic-smoke-' + [Guid]::NewGuid().ToString('N') + '.bas'
            Submit "LOAD $name" 'file not found'
            $created = $false
            try {
                Submit "NEW $name" "`r`nBASIC> "
                Submit 'EDIT' 'Structured BASIC editor'
                $serial.Write('PRINT "SD TEST"' + "`r" + [char]19)
                [void](Receive-Until 'Saved.' 14000)
                $created = $true
                $serial.Write([string][char]17)
                [void](Receive-Until 'BASIC> ')
                [void](Drain)
                Submit 'NEW' "`r`nBASIC> "
                Submit "LOAD $name" 'Loaded.'
                Submit 'RUN' "`r`nSD TEST`r`nBASIC> "
                Submit 'APPEND PRINT 987' "`r`nBASIC> "
                Submit 'SAVE' 'Saved.' 14000
                Submit "RUN $name" "`r`nSD TEST`r`n987`r`nBASIC> "
                Submit "TYPE $name" "`r`nPRINT 987`r`n"
                Write-Output 'SD: editor Ctrl+S, LOAD, SAVE replacement, RUN file and TYPE PASS'
            } finally {
                if ($created) { Submit "DEL $name" 'File deleted.' }
            }
        }
    }
    Write-Output "COM smoke PASS on $Port (115200 8N1)."
} finally {
    $serial.Dispose()
    if ($Log) { [System.IO.File]::WriteAllText($Log, $transcript.ToString()) }
}
