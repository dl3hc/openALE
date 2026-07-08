param([int]$Port = 8770)
$ErrorActionPreference = 'Stop'

function Send-Text($ws, $text) {
    $seg = [System.ArraySegment[byte]]::new([System.Text.Encoding]::UTF8.GetBytes($text))
    $ws.SendAsync($seg, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).Wait()
}
function Recv-Text($ws) {
    $buf = New-Object byte[] 65536
    $sb = New-Object System.Text.StringBuilder
    do {
        $seg = [System.ArraySegment[byte]]::new($buf)
        $res = $ws.ReceiveAsync($seg, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        if ($res.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Binary) { return $null }
        [void]$sb.Append([System.Text.Encoding]::UTF8.GetString($buf, 0, $res.Count))
    } while (-not $res.EndOfMessage)
    return $sb.ToString()
}
function Wait-Reply($ws, $id) {
    for ($i = 0; $i -lt 200; $i++) {
        $msg = Recv-Text $ws; if ($null -eq $msg) { continue }
        try { $obj = $msg | ConvertFrom-Json } catch { continue }
        if ($obj.id -eq $id) { return $obj }
    }
    return $null
}
function ListNets($ws, $id, $label) {
    Send-Text $ws ('{{"id":{0},"cmd":"NETS_LIST"}}' -f $id)
    $r = Wait-Reply $ws $id
    Write-Host "`n--- NETS_LIST ($label) ---"
    if ($r.data.Count -eq 0) { Write-Host "  (no nets)" }
    $r.data | ForEach-Object { Write-Host ("  {0}: dwell_ms={1} chans=[{2}]" -f $_.name, $_.dwell_ms, ($_.channel_ids -join ',')) }
    return $r
}

$ws = New-Object System.Net.WebSockets.ClientWebSocket
$ws.ConnectAsync([Uri]("ws://127.0.0.1:$Port/"), [Threading.CancellationToken]::None).Wait()
Write-Host "connected: $($ws.State)"

# add a channel + net (mirrors GUI addCh/addNet + NET_ASSIGN)
Send-Text $ws '{"id":1,"cmd":"CHANNEL_ADD","rx_hz":14109000,"tx_hz":14109000,"mode":"USB","id":"C-1"}'; [void](Wait-Reply $ws 1)
Send-Text $ws '{"id":2,"cmd":"NET_ADD","name":"NET1"}'; [void](Wait-Reply $ws 2)
ListNets $ws 3 "after NET_ADD" | Out-Null

# GUI netPolicySet: set dwell=500 (full payload as the GUI sends it)
Send-Text $ws '{"id":4,"cmd":"NET_UPDATE","name":"NET1","dwell_ms":500,"scanning_enabled":true,"sounding_enabled":false,"sounding_interval_sec":300,"calling_length_c":10}'
$u = Wait-Reply $ws 4; Write-Host "`nNET_UPDATE dwell=500 ok=$($u.ok)"
ListNets $ws 5 "after NET_UPDATE(500)" | Out-Null

# simulate GUI closeSettings(): CHANNELS_LIST then NETS_LIST
Send-Text $ws '{"id":6,"cmd":"CHANNELS_LIST"}'; [void](Wait-Reply $ws 6)
ListNets $ws 7 "after simulated closeSettings sync" | Out-Null

$ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, "", [Threading.CancellationToken]::None).Wait()
Write-Host "`ndone."
