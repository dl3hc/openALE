param([int]$Port = 8770)
$ErrorActionPreference = 'Stop'

function Send-Text($ws, $text) {
    $seg = [System.ArraySegment[byte]]::new([System.Text.Encoding]::UTF8.GetBytes($text))
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).Wait()
}
# Receive one frame with a timeout (ms). Returns text, or $null on timeout/binary.
function Recv-Text($ws, $timeoutMs = 1500) {
    $buf = New-Object byte[] 65536
    $sb = New-Object System.Text.StringBuilder
    $cts = New-Object System.Threading.CancellationTokenSource
    $cts.CancelAfter($timeoutMs)
    try {
        do {
            $seg = [System.ArraySegment[byte]]::new($buf)
            $res = $ws.ReceiveAsync($seg, $cts.Token).GetAwaiter().GetResult()
            if ($res.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Binary) { return $null }
            [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $res.Count))
        } while (-not $res.EndOfMessage)
    } catch { return $null }
    return $sb.ToString()
}
# Send then collect the reply with matching id (drains events up to a time budget)
function Rpc($ws, $id, $json) {
    Send-Text $ws $json
    for ($i = 0; $i -lt 40; $i++) {
        $m = Recv-Text $ws 1500
        if ($null -eq $m) { continue }
        try { $o = $m | ConvertFrom-Json } catch { continue }
        if ($o.id -eq $id) { return $o }
    }
    return $null
}
function ShowNets($ws, $id, $label) {
    $r = Rpc $ws $id ('{{"id":{0},"cmd":"NETS_LIST"}}' -f $id)
    Write-Host "`n--- NETS_LIST ($label) ---"
    if (-not $r) { Write-Host "  (no reply)"; return }
    if ($r.data.Count -eq 0) { Write-Host "  (no nets)" }
    $r.data | ForEach-Object { Write-Host ("  {0}: dwell_ms={1} chans=[{2}]" -f $_.name, $_.dwell_ms, ($_.channel_ids -join ',')) }
}

$ws = New-Object System.Net.WebSockets.ClientWebSocket
$ws.ConnectAsync([Uri]("ws://127.0.0.1:$Port/"), [Threading.CancellationToken]::None).Wait()
Write-Host "connected: $($ws.State)"

[void](Rpc $ws 1 '{"id":1,"cmd":"CHANNEL_ADD","rx_hz":14109000,"tx_hz":14109000,"mode":"USB","label":""}')
[void](Rpc $ws 2 '{"id":2,"cmd":"NET_ADD","name":"NET1"}')
ShowNets $ws 3 "after NET_ADD"

$u = Rpc $ws 4 '{"id":4,"cmd":"NET_UPDATE","name":"NET1","dwell_ms":500,"scanning_enabled":true,"sounding_enabled":false,"sounding_interval_sec":300,"calling_length_c":10}'
Write-Host "`nNET_UPDATE dwell=500 ok=$($u.ok)"
ShowNets $ws 5 "after NET_UPDATE(500)"

# simulate GUI closeSettings(): CHANNELS_LIST then NETS_LIST
[void](Rpc $ws 6 '{"id":6,"cmd":"CHANNELS_LIST"}')
ShowNets $ws 7 "after simulated closeSettings sync"

$ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).Wait()
Write-Host "`ndone."
