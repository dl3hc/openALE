param(
    [int]$Port = 8765,
    [int]$SetDwell = 0   # 0 = read-only; otherwise send NET_UPDATE with this dwell to the first net
)

$ErrorActionPreference = 'Stop'

function Send-Text($ws, $text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    $seg = [System.ArraySegment[byte]]::new($bytes)
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).Wait()
}

function Recv-Text($ws) {
    $buf = New-Object byte[] 65536
    $sb = New-Object System.Text.StringBuilder
    do {
        $seg = [System.ArraySegment[byte]]::new($buf)
        $res = $ws.ReceiveAsync($seg, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($res.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Binary) {
            # skip binary spectrum frames
            return $null
        }
        [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $res.Count))
    } while (-not $res.EndOfMessage)
    return $sb.ToString()
}

# Wait for a JSON reply that has the given id (skips events/binary)
function Wait-Reply($ws, $id) {
    for ($i = 0; $i -lt 200; $i++) {
        $msg = Recv-Text $ws
        if ($null -eq $msg) { continue }
        try { $obj = $msg | ConvertFrom-Json } catch { continue }
        if ($obj.id -eq $id) { return $obj }
    }
    return $null
}

$ws = New-Object System.Net.WebSockets.ClientWebSocket
$uri = [Uri]("ws://127.0.0.1:$Port/")
$ws.ConnectAsync($uri, [Threading.CancellationToken]::None).Wait()
Write-Host "connected: $($ws.State)"

# 1) initial NETS_LIST
Send-Text $ws '{"id":1,"cmd":"NETS_LIST"}'
$r1 = Wait-Reply $ws 1
Write-Host "`n--- NETS_LIST (before) ---"
$r1.data | ForEach-Object { Write-Host ("  {0}: dwell_ms={1} chans=[{2}]" -f $_.name, $_.dwell_ms, ($_.channel_ids -join ',')) }

if ($SetDwell -gt 0 -and $r1.data.Count -gt 0) {
    $n = $r1.data[0]
    $upd = @{
        id = 2; cmd = 'NET_UPDATE'; name = $n.name
        dwell_ms = $SetDwell
        scanning_enabled = $n.scanning_enabled
        sounding_enabled = $n.sounding_enabled
        sounding_interval_sec = $n.sounding_interval_sec
        calling_length_c = $n.calling_length_c
    } | ConvertTo-Json -Compress
    Write-Host "`n--- NET_UPDATE -> $($n.name) dwell=$SetDwell ---"
    Send-Text $ws $upd
    $r2 = Wait-Reply $ws 2
    Write-Host "  ok=$($r2.ok)"

    Send-Text $ws '{"id":3,"cmd":"NETS_LIST"}'
    $r3 = Wait-Reply $ws 3
    Write-Host "`n--- NETS_LIST (after NET_UPDATE) ---"
    $r3.data | ForEach-Object { Write-Host ("  {0}: dwell_ms={1}" -f $_.name, $_.dwell_ms) }

    # simulate what the GUI does on closeSettings(): CHANNELS_LIST then NETS_LIST again
    Send-Text $ws '{"id":4,"cmd":"CHANNELS_LIST"}'
    [void](Wait-Reply $ws 4)
    Send-Text $ws '{"id":5,"cmd":"NETS_LIST"}'
    $r5 = Wait-Reply $ws 5
    Write-Host "`n--- NETS_LIST (after simulated closeSettings sync) ---"
    $r5.data | ForEach-Object { Write-Host ("  {0}: dwell_ms={1}" -f $_.name, $_.dwell_ms) }
}

# Also show TIMING_GET scan_dwell_ms (the global)
Send-Text $ws '{"id":9,"cmd":"TIMING_GET"}'
$r9 = Wait-Reply $ws 9
Write-Host "`n--- TIMING_GET ---"
Write-Host ("  global scan_dwell_ms = {0}" -f $r9.scan_dwell_ms)

$ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).Wait()
Write-Host "`ndone."
