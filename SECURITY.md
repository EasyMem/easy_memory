# Security Policy

Since EasyMem is a memory management library (often used in performance-critical or bare-metal environments), any bugs related to pointer tagging, LLRB-tree integrity, or XOR-magic validation are treated as critical security vulnerabilities.

## Supported Versions

Currently, only the `main` branch and the latest active development release (v0.5.x) are supported with security updates. 

| Version | Supported          |
| ------- | ------------------ |
| 0.5.x   | :white_check_mark: |
| < 0.5.0 | :x:                |

## Reporting a Vulnerability

If you have discovered a vulnerability that allows heap corruption, arbitrary code execution, or bypasses the internal safety checks, **please do not open a public GitHub issue**.

Instead, please report it privately:
1. Email me directly at: **gooderfreed@gmail.com**
2. Provide a brief description of the exploit.
3. If possible, include a minimal reproducible C snippet or a `libFuzzer` crash dump.

I will acknowledge receipt of your vulnerability report within 48 hours and work with you to patch it before public disclosure.
