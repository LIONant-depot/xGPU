# Post-fetch patch for imgui-node-editor (thedmd/imgui-node-editor) -- a third-party repo
# we don't own, so this fix can't be pushed upstream. FetchAndPopulate re-clones it fresh on
# every clean configure, so it has to be re-applied here every time.
#
# imgui_extra_math.h/.inl unconditionally declare+define operator*(float, ImVec2), duplicating
# imgui.h's own definition once IMGUI_DEFINE_MATH_OPERATORS is set (imgui provides this overload
# from version 18955 onward). Guard it the same way the neighboring operator- already is.

$ErrorActionPreference = 'Stop'

function Patch-Once {
    param($Path, $DetectPattern, $Pattern, $Replacement)

    if (-not (Test-Path $Path)) {
        throw "Not found: $Path"
    }
    $content = Get-Content $Path -Raw
    if ($content -match $DetectPattern) {
        Write-Host "Already patched, skipping: $Path"
        return
    }
    if ($content -notmatch $Pattern) {
        Write-Host "WARNING: expected pattern not found (upstream file changed?): $Path"
        return
    }
    $patched = [regex]::Replace($content, $Pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $Replacement }, 'Singleline')
    Set-Content -Path $Path -Value $patched -NoNewline
    Write-Host "Patched: $Path"
}

$root = "..\dependencies\imgui-node-editor"

Patch-Once -Path "$root\imgui_extra_math.h" `
    -DetectPattern '# if IMGUI_VERSION_NUM < 18955\r?\ninline ImVec2 operator\*\(const float lhs, const ImVec2& rhs\);\r?\n# endif' `
    -Pattern 'inline ImVec2 operator\*\(const float lhs, const ImVec2& rhs\);' `
    -Replacement "# if IMGUI_VERSION_NUM < 18955`r`ninline ImVec2 operator*(const float lhs, const ImVec2& rhs);`r`n# endif"

Patch-Once -Path "$root\imgui_extra_math.inl" `
    -DetectPattern '# if IMGUI_VERSION_NUM < 18955\r?\ninline ImVec2 operator\*\(const float lhs, const ImVec2& rhs\)\r?\n\{\r?\n    return ImVec2\(lhs \* rhs\.x, lhs \* rhs\.y\);\r?\n\}\r?\n# endif' `
    -Pattern 'inline ImVec2 operator\*\(const float lhs, const ImVec2& rhs\)\r?\n\{\r?\n    return ImVec2\(lhs \* rhs\.x, lhs \* rhs\.y\);\r?\n\}' `
    -Replacement "# if IMGUI_VERSION_NUM < 18955`r`ninline ImVec2 operator*(const float lhs, const ImVec2& rhs)`r`n{`r`n    return ImVec2(lhs * rhs.x, lhs * rhs.y);`r`n}`r`n# endif"
