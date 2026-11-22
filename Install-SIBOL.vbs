' ============================================================
'  SIBOL desktop installer  ·  Windows 10 / 11  ·  fully offline
'  Put this file beside SIBOL.html, then double-click it.
'  It generates its own icon, copies the app to a stable folder,
'  and creates Desktop + Start Menu shortcuts that open SIBOL
'  in its own window. No internet, no Node, no admin rights.
'  Uninstall:  wscript Install-SIBOL.vbs uninstall
' ============================================================

Dim fso, sh, appDir, htmlDst, icoPath, srcHtml, scriptDir
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh  = CreateObject("WScript.Shell")

scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
appDir    = sh.ExpandEnvironmentStrings("%LOCALAPPDATA%") & "\SIBOL"
htmlDst   = appDir & "\SIBOL.html"
icoPath   = appDir & "\SIBOL.ico"

Dim deskLnk, startDir, startLnk, startLnk2
deskLnk   = sh.SpecialFolders("Desktop") & "\SIBOL.lnk"
startDir  = sh.SpecialFolders("StartMenu") & "\Programs"
startLnk  = startDir & "\SIBOL.lnk"
startLnk2 = startDir & "\SIBOL (browser).lnk"

' ---------- uninstall ----------
If WScript.Arguments.Count > 0 Then
  If LCase(WScript.Arguments(0)) = "uninstall" Then
    On Error Resume Next
    If fso.FileExists(deskLnk)   Then fso.DeleteFile deskLnk, True
    If fso.FileExists(startLnk)  Then fso.DeleteFile startLnk, True
    If fso.FileExists(startLnk2) Then fso.DeleteFile startLnk2, True
    If fso.FolderExists(appDir)  Then fso.DeleteFolder appDir, True
    On Error GoTo 0
    MsgBox "SIBOL removed from this PC.", 64, "SIBOL"
    WScript.Quit 0
  End If
End If

' ---------- locate the app file ----------
srcHtml = ""
If fso.FileExists(scriptDir & "\SIBOL.html") Then
  srcHtml = scriptDir & "\SIBOL.html"
ElseIf fso.FileExists(scriptDir & "\index.html") Then
  srcHtml = scriptDir & "\index.html"
ElseIf fso.FileExists(appDir & "\SIBOL.html") Then
  srcHtml = appDir & "\SIBOL.html"
End If

If srcHtml = "" Then
  MsgBox "SIBOL.html was not found." & vbCrLf & vbCrLf & _
         "Put SIBOL.html in the same folder as this installer:" & vbCrLf & _
         scriptDir, 16, "SIBOL installer"
  WScript.Quit 1
End If

If Not fso.FolderExists(appDir) Then fso.CreateFolder appDir
If Not fso.FolderExists(startDir) Then fso.CreateFolder startDir
fso.CopyFile srcHtml, htmlDst, True

' ---------- helpers ----------
Function Mx(a, b)
  If a > b Then Mx = a Else Mx = b
End Function

Function Mn(a, b)
  If a < b Then Mn = a Else Mn = b
End Function

Function Cl01(v)
  If v < 0 Then
    Cl01 = 0
  ElseIf v > 1 Then
    Cl01 = 1
  Else
    Cl01 = v
  End If
End Function

Function HB(v)
  Dim s
  s = Hex(CInt(v) And 255)
  If Len(s) < 2 Then s = "0" & s
  HB = LCase(s)
End Function

Function LE16(v)
  LE16 = HB(v And 255) & HB((v \ 256) And 255)
End Function

Function LE32(v)
  LE32 = HB(v And 255) & HB((v \ 256) And 255) & HB((v \ 65536) And 255) & HB((v \ 16777216) And 255)
End Function

Sub PushHex(ByRef arr, ByRef idx, h)
  Dim k
  For k = 1 To Len(h) Step 2
    arr(idx) = Mid(h, k, 2)
    idx = idx + 1
  Next
End Sub

Function SegD(px, py, ax, ay, bx, by, r)
  Dim dx, dy, ux, uy, h
  dx = bx - ax : dy = by - ay : ux = px - ax : uy = py - ay
  h = Cl01((ux * dx + uy * dy) / (dx * dx + dy * dy))
  SegD = Sqr((ux - dx * h) ^ 2 + (uy - dy * h) ^ 2) - r
End Function

Function LensD(px, py, cx, cy, r, ang, off)
  Dim dx, dy, ca, sa, u, v
  dx = px - cx : dy = py - cy : ca = Cos(ang) : sa = Sin(ang)
  u = dx * ca + dy * sa : v = -dx * sa + dy * ca
  LensD = Mx(Sqr((u - off) ^ 2 + v ^ 2) - r, Sqr((u + off) ^ 2 + v ^ 2) - r)
End Function

' Builds one 32-bit BMP icon image (BITMAPINFOHEADER + XOR + AND mask) as hex
Function IconImage(sz)
  Dim parts(), pi, x, y, px, py
  Dim dxr, dyr, sd, aBg, cov, cr, cg, cb, i, maskBytes
  ReDim parts(80000)
  pi = 0

  PushHex parts, pi, LE32(40)
  PushHex parts, pi, LE32(sz)
  PushHex parts, pi, LE32(sz * 2)
  PushHex parts, pi, LE16(1)
  PushHex parts, pi, LE16(32)
  PushHex parts, pi, LE32(0)
  PushHex parts, pi, LE32(0)
  PushHex parts, pi, LE32(0)
  PushHex parts, pi, LE32(0)
  PushHex parts, pi, LE32(0)
  PushHex parts, pi, LE32(0)

  For y = sz - 1 To 0 Step -1
    For x = 0 To sz - 1
      px = (x + 0.5) / sz
      py = (y + 0.5) / sz

      dxr = Abs(px - 0.5) - 0.5 + 0.22
      dyr = Abs(py - 0.5) - 0.5 + 0.22
      sd = Sqr(Mx(dxr, 0) ^ 2 + Mx(dyr, 0) ^ 2) + Mn(Mx(dxr, dyr), 0) - 0.22
      aBg = Cl01(0.5 - sd * sz)

      If aBg <= 0 Then
        PushHex parts, pi, "00000000"
      Else
        cr = 7 : cg = 19 : cb = 12

        cov = Cl01(0.5 - SegD(px, py, 0.5, 0.80, 0.5, 0.44, 0.045) * sz)
        cr = cr * (1 - cov) + 31 * cov
        cg = cg * (1 - cov) + 199 * cov
        cb = cb * (1 - cov) + 122 * cov

        cov = Cl01(0.5 - LensD(px, py, 0.360, 0.375, 0.235, -0.60, 0.130) * sz)
        cr = cr * (1 - cov) + 201 * cov
        cg = cg * (1 - cov) + 247 * cov
        cb = cb * (1 - cov) + 92 * cov

        cov = Cl01(0.5 - LensD(px, py, 0.640, 0.450, 0.200, 0.60, 0.115) * sz)
        cr = cr * (1 - cov) + 31 * cov
        cg = cg * (1 - cov) + 199 * cov
        cb = cb * (1 - cov) + 122 * cov

        cov = Cl01(0.5 - (Sqr((px - 0.5) ^ 2 + (py - 0.82) ^ 2) - 0.055) * sz)
        cr = cr * (1 - cov) + 233 * cov
        cg = cg * (1 - cov) + 220 * cov
        cb = cb * (1 - cov) + 195 * cov

        PushHex parts, pi, HB(cb) & HB(cg) & HB(cr) & HB(aBg * 255)
      End If
    Next
  Next

  maskBytes = (sz \ 8) * sz
  For i = 1 To maskBytes
    PushHex parts, pi, "00"
  Next

  ReDim Preserve parts(pi - 1)
  IconImage = Join(parts, "")
End Function

Function WriteBinary(path, hexStr)
  Dim dom, el, bytes, st
  On Error Resume Next
  Set dom = CreateObject("Msxml2.DOMDocument.6.0")
  If dom Is Nothing Then Set dom = CreateObject("Microsoft.XMLDOM")
  On Error GoTo 0
  Set el = dom.createElement("b64")
  el.dataType = "bin.hex"
  el.Text = hexStr
  bytes = el.nodeTypedValue
  Set st = CreateObject("ADODB.Stream")
  st.Type = 1
  st.Open
  st.Write bytes
  st.SaveToFile path, 2
  st.Close
  WriteBinary = True
End Function

' ---------- build the .ico (32 px + 64 px) ----------
Dim img32, img64, n32, n64, off1, off2, icoHex
img32 = IconImage(32)
img64 = IconImage(64)
n32 = Len(img32) \ 2
n64 = Len(img64) \ 2
off1 = 6 + 16 * 2
off2 = off1 + n32

icoHex = LE16(0) & LE16(1) & LE16(2)
icoHex = icoHex & HB(32) & HB(32) & HB(0) & HB(0) & LE16(1) & LE16(32) & LE32(n32) & LE32(off1)
icoHex = icoHex & HB(64) & HB(64) & HB(0) & HB(0) & LE16(1) & LE16(32) & LE32(n64) & LE32(off2)
icoHex = icoHex & img32 & img64

Dim iconOk
iconOk = True
On Error Resume Next
WriteBinary icoPath, icoHex
If Err.Number <> 0 Then
  iconOk = False
  Err.Clear
End If
On Error GoTo 0

' ---------- find a browser that can open an app window ----------
Dim cands, browser, i
cands = Array( _
  sh.ExpandEnvironmentStrings("%ProgramFiles%") & "\Google\Chrome\Application\chrome.exe", _
  sh.ExpandEnvironmentStrings("%ProgramFiles(x86)%") & "\Google\Chrome\Application\chrome.exe", _
  sh.ExpandEnvironmentStrings("%LOCALAPPDATA%") & "\Google\Chrome\Application\chrome.exe", _
  sh.ExpandEnvironmentStrings("%ProgramFiles(x86)%") & "\Microsoft\Edge\Application\msedge.exe", _
  sh.ExpandEnvironmentStrings("%ProgramFiles%") & "\Microsoft\Edge\Application\msedge.exe")

browser = ""
For i = 0 To UBound(cands)
  If fso.FileExists(cands(i)) Then
    browser = cands(i)
    Exit For
  End If
Next

Dim fileUrl, args
fileUrl = "file:///" & Replace(htmlDst, "\", "/")
fileUrl = Replace(fileUrl, " ", "%20")
args = "--app=" & Chr(34) & fileUrl & Chr(34) & _
       " --window-size=1480,940 --no-first-run --no-default-browser-check"

' ---------- shortcuts ----------
Dim lnk, madeApp
madeApp = (browser <> "")

If madeApp Then
  Set lnk = sh.CreateShortcut(deskLnk)
  lnk.TargetPath = browser
  lnk.Arguments = args
  lnk.WorkingDirectory = appDir
  lnk.Description = "SIBOL offline node"
  If iconOk Then lnk.IconLocation = icoPath & ", 0"
  lnk.WindowStyle = 1
  lnk.Save

  Set lnk = sh.CreateShortcut(startLnk)
  lnk.TargetPath = browser
  lnk.Arguments = args
  lnk.WorkingDirectory = appDir
  lnk.Description = "SIBOL offline node"
  If iconOk Then lnk.IconLocation = icoPath & ", 0"
  lnk.WindowStyle = 1
  lnk.Save
End If

' plain-browser fallback always present in Start Menu
Set lnk = sh.CreateShortcut(startLnk2)
lnk.TargetPath = htmlDst
lnk.WorkingDirectory = appDir
lnk.Description = "SIBOL offline node (opens in default browser)"
If iconOk Then lnk.IconLocation = icoPath & ", 0"
lnk.Save

Dim msg
msg = "SIBOL installed." & vbCrLf & vbCrLf & _
      "App folder:  " & appDir & vbCrLf & _
      "Desktop:     SIBOL" & vbCrLf & _
      "Start Menu:  SIBOL  /  SIBOL (browser)" & vbCrLf & vbCrLf & _
      "To run a demo:" & vbCrLf & _
      "  1. Power the node." & vbCrLf & _
      "  2. Join Wi-Fi  SIBOL-NODE  (open, no password)." & vbCrLf & _
      "  3. Open SIBOL from the Desktop." & vbCrLf & _
      "  4. Enter the IP and CODE shown on the OLED." & vbCrLf & vbCrLf & _
      "No internet is used at any point."

If Not madeApp Then
  msg = msg & vbCrLf & vbCrLf & _
        "Chrome/Edge was not found, so the shortcut opens SIBOL in your " & _
        "default browser instead of a separate app window."
End If
If Not iconOk Then
  msg = msg & vbCrLf & vbCrLf & _
        "The icon file could not be written on this PC; the shortcut uses " & _
        "the browser icon. Everything else works."
End If

MsgBox msg, 64, "SIBOL"