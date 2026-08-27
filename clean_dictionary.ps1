# clean_dictionary.ps1
# 清理 dictionary.data 中频率为 0 的多字词（保留频率为 0 的单字）
# 用法: powershell -ExecutionPolicy Bypass -File clean_dictionary.ps1 [文件路径]

param(
    [string]$Path = "dictionary.data"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Error "找不到文件: $Path"
    exit 1
}

$fullPath = (Resolve-Path -LiteralPath $Path).Path

# 1. 备份原文件
$backup = "$fullPath.bak"
Copy-Item -LiteralPath $fullPath -Destination $backup -Force
Write-Host "已备份原文件到: $backup"

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

# 2. 读取（UTF-8 无 BOM）
$content = [System.IO.File]::ReadAllText($fullPath, $utf8NoBom)
$content = $content -replace "`r`n", "`n"
$lines = $content -split "`n"

$output = New-Object System.Collections.Generic.List[string]
$removedWords = 0
$keptWords = 0
$removedLines = 0

# 3. 逐行处理
foreach ($line in $lines) {
    if ($line -eq "") { continue }

    $tokens = @($line -split "\s+" | Where-Object { $_ -ne "" })
    if ($tokens.Count -lt 2) {
        # 格式异常的行原样保留
        $output.Add($line)
        continue
    }

    $pinyin = $tokens[0]
    $count = [int]$tokens[1]

    $pairs = New-Object System.Collections.Generic.List[string]
    for ($i = 2; $i -lt $tokens.Count; $i += 2) {
        if ($i + 1 -ge $tokens.Count) { break }
        $word = $tokens[$i]
        $freq = [long]$tokens[$i + 1]
        $keptWords++
        if ($freq -le 16 -and $word.Length -gt 1) {
            $removedWords++
        } else {
            $pairs.Add("$word $freq")
        }
    }

    if ($pairs.Count -eq 0) {
        # 该拼音下已无词，跳过整行
        $removedLines++
        continue
    }

    # 重建行，保持原格式: pinyin  数量  词1 频率1  词2 频率2  ...
    $newLine = "$pinyin  $($pairs.Count)  " + ($pairs -join "  ") + "  "
    $output.Add($newLine)
}

# 4. 写回（UTF-8 无 BOM，LF 换行）
$result = ($output -join "`n") + "`n"
[System.IO.File]::WriteAllText($fullPath, $result, $utf8NoBom)

Write-Host "处理完成。"
Write-Host "移除频率为 0 的多字词: $removedWords"
Write-Host "保留词语总数: $($keptWords - $removedWords)"
Write-Host "整行被清空的拼音条目: $removedLines"
