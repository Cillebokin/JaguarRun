$ErrorActionPreference = "Stop"

$expectedRustcRelease = "1.98.1"
$expectedRustcCommit = "48a229ceaefd4985c50990b14116b6d856af0985"
$expectedCargo = "cargo 1.98.1 (797e8a9bc 2026-08-05)"
$target = "x86_64-pc-windows-msvc"

Push-Location $PSScriptRoot
try {
    $rustcInfo = (& rustc -vV) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "rustc -vV failed with exit code $LASTEXITCODE"
    }
    if ($rustcInfo -notmatch "(?m)^release: $([regex]::Escape($expectedRustcRelease))$" -or
        $rustcInfo -notmatch "(?m)^commit-hash: $([regex]::Escape($expectedRustcCommit))$" -or
        $rustcInfo -notmatch "(?m)^host: $([regex]::Escape($target))$") {
        throw "Rust compiler does not match the Sparrow reference baseline`n$rustcInfo"
    }

    $cargoVersion = (& cargo --version).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "cargo --version failed with exit code $LASTEXITCODE"
    }
    if ($cargoVersion -ne $expectedCargo) {
        throw "Cargo does not match the Sparrow reference baseline: $cargoVersion"
    }

    & cargo fmt --check --manifest-path Cargo.toml
    if ($LASTEXITCODE -ne 0) {
        throw "cargo fmt --check failed with exit code $LASTEXITCODE"
    }

    & cargo test --locked --manifest-path Cargo.toml --target $target
    if ($LASTEXITCODE -ne 0) {
        throw "Debug reference tests failed with exit code $LASTEXITCODE"
    }

    & cargo test --locked --release --manifest-path Cargo.toml --target $target
    if ($LASTEXITCODE -ne 0) {
        throw "Release reference tests failed with exit code $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
