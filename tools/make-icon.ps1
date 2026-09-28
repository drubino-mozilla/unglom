# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Draws the Unglom icon (a "U" over a taskbar-style running indicator) and
# writes res\unglom.ico plus res\unglom-preview.png.
param([string] $OutDir = (Join-Path $PSScriptRoot "..\res"))

Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$top = [System.Drawing.Color]::FromArgb(255, 0x4C, 0xC2, 0xFF)
$bottom = [System.Drawing.Color]::FromArgb(255, 0x00, 0x67, 0xC0)

function New-IconBitmap([int] $size) {
  $bitmap = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bitmap)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.Clear([System.Drawing.Color]::Transparent)

  # Geometry in a unit square, scaled to the bitmap. Small sizes snap edges to
  # whole pixels so they stay crisp.
  $s = [single] $size
  $snap = $size -le 48
  $stroke = 0.17 * $s
  if ($snap) { $stroke = [Math]::Round($stroke) }
  $left = 0.29 * $s
  if ($snap) { $left = [Math]::Round($left - $stroke / 2) + $stroke / 2 }
  $right = $s - $left
  $radius = ($right - $left) / 2
  $upTop = 0.08 * $s + $stroke / 2
  $bendY = 0.66 * $s - $radius

  $path = New-Object System.Drawing.Drawing2D.GraphicsPath
  $path.AddLine($left, $upTop, $left, $bendY)
  $path.AddArc($left, $bendY - $radius, 2 * $radius, 2 * $radius, 180, -180)
  $path.AddLine($right, $bendY, $right, $upTop)

  $gradientRect = New-Object System.Drawing.RectangleF 0, 0, $s, $s
  $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush $gradientRect, $top, $bottom, 90.0
  $pen = New-Object System.Drawing.Pen $brush, $stroke
  $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
  $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
  $g.DrawPath($pen, $path)

  # The running indicator: a short rounded pill near the bottom edge.
  $barHeight = 0.085 * $s
  $barWidth = 0.34 * $s
  $barBottom = 0.93 * $s
  if ($snap) {
    $barHeight = [Math]::Max(2, [Math]::Round($barHeight))
    $barWidth = [Math]::Round($barWidth)
    if (($barWidth % 2) -ne ($size % 2)) { $barWidth += 1 }
    $barBottom = [Math]::Round($barBottom)
  }
  $barX = ($s - $barWidth) / 2
  $barY = $barBottom - $barHeight
  $bar = New-Object System.Drawing.Drawing2D.GraphicsPath
  $d = $barHeight
  $bar.AddArc($barX, $barY, $d, $d, 90, 180)
  $bar.AddArc($barX + $barWidth - $d, $barY, $d, $d, 270, 180)
  $bar.CloseFigure()
  $barBrush = New-Object System.Drawing.SolidBrush $bottom
  $g.FillPath($barBrush, $bar)

  $g.Dispose()
  return $bitmap
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$pngs = @()
foreach ($size in $sizes) {
  $bitmap = New-IconBitmap $size
  $stream = New-Object System.IO.MemoryStream
  $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
  $pngs += , @($size, $stream.ToArray())
  $bitmap.Dispose()
}

# ICO container with PNG-compressed entries (supported since Windows Vista).
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([UInt16] 0); $w.Write([UInt16] 1); $w.Write([UInt16] $pngs.Count)
$offset = 6 + 16 * $pngs.Count
foreach ($entry in $pngs) {
  $size = $entry[0]; $data = $entry[1]
  $dim = if ($size -ge 256) { 0 } else { $size }
  $w.Write([byte] $dim); $w.Write([byte] $dim); $w.Write([byte] 0); $w.Write([byte] 0)
  $w.Write([UInt16] 1); $w.Write([UInt16] 32)
  $w.Write([UInt32] $data.Length); $w.Write([UInt32] $offset)
  $offset += $data.Length
}
foreach ($entry in $pngs) { $w.Write([byte[]] $entry[1]) }
$w.Flush()
[System.IO.File]::WriteAllBytes((Join-Path $OutDir "unglom.ico"), $out.ToArray())

# Preview: every size at 1x on dark and light strips, plus 16/24/32 magnified.
$preview = New-Object System.Drawing.Bitmap 900, 420
$g = [System.Drawing.Graphics]::FromImage($preview)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(32, 32, 32))), 0, 0, 900, 210)
$g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(243, 243, 243))), 0, 210, 900, 210)
foreach ($row in 0, 1) {
  $x = 10
  foreach ($size in 16, 24, 32, 48) {
    $b = New-IconBitmap $size
    $g.DrawImage($b, $x, 10 + 210 * $row + (48 - $size), $size, $size)
    $x += $size + 12
    $b.Dispose()
  }
  $b = New-IconBitmap 256
  $g.DrawImage($b, 190, 10 + 210 * $row, 190, 190)
  $b.Dispose()
  $x = 400
  foreach ($size in 16, 24, 32) {
    $b = New-IconBitmap $size
    $g.DrawImage($b, $x, 10 + 210 * $row, $size * 6, $size * 6)
    $x += $size * 6 + 10
    $b.Dispose()
  }
}
$g.Dispose()
$preview.Save((Join-Path $OutDir "unglom-preview.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()
"Wrote $(Join-Path $OutDir 'unglom.ico')"
