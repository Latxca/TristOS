# Security Policy

## Supported Versions

Security updates and kernel/subsystem patches are provided for the active development branch and the latest release of **TristOS (x86-64)**. Older experimental builds do not receive backported fixes.

| Version / Branch | Architecture | Supported |
| :--- | :--- | :--- |
| `main` (Active Development) | x86-64 (Intel / AMD) | :white_check_mark: |
| Latest Release (`v3.4`) | x86-64 (Intel / AMD) | :white_check_mark: |
| Older Pre-releases / Legacy | All | :x: |

---

## Scope & Threat Model

Because **TristOS** separates Ring 0 kernel execution from Ring 3 user-mode applications, security reports are prioritized based on boundary impact:

### In Scope
* **Kernel & Ring 0 Isolation (`T:\TristOS\System504\`):** Privilege escalation from Ring 3 to Ring 0, arbitrary kernel memory read/write, syscall pointer/bounds validation bypasses, and kernel panics triggered by unprivileged user-mode code.
* **Executable Loaders (`.texe` and `.exe`):** Buffer overflows, integer overflows, or malformed header parsing bugs in the native `.texe` loader or the `.exe` compatibility subsystem.
* **Filesystem & Access Control (`T:\`):** Bypasses of directory ownership or ACL restrictions (such as unprivileged writes to `T:\TristOS\System504\`, `T:\TristOS\Config\`, or another user's `T:\Users\<Username>\` profile).
* **Core System Services & Drivers (`T:\TristOS\Drivers\`):** Vulnerabilities in bundled system services or kernel-mode drivers.

### Out of Scope
* Standard Ring 3 application crashes (e.g., null pointer dereference or segmentation fault inside a user-mode `.texe` or `.exe` app) that are properly caught and terminated by the kernel without affecting system stability or other processes.
* Issues requiring physical hardware access or administrative (`Administrators` / `TristOS_TrustedKernel`) privileges already obtained.
* Third-party `.exe` binaries run under the compatibility layer where the bug originates in the third-party software rather than TristOS.

---

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub Issues, Discussions, or Pull Requests.**

### Preferred Method: GitHub Private Vulnerability Reporting
1. Navigate to the **Security** tab of the official TristOS repository.
2. Click **Report a vulnerability** to open a private security advisory draft.
3. Provide as much detail as possible so the issue can be reproduced and patched quickly.

*(Note: If private reporting is temporarily unavailable, open a minimal issue asking a maintainer for a private contact channel—do not include exploit details in the public issue.)*

### What to Include in Your Report
* **Component affected:** Kernel (`tristoskrnl.texe`), syscall interface, `.texe`/`.exe` loader, driver, or user-mode system service.
* **Build / Commit:** The branch, release tag, or commit hash tested.
* **Environment:** CPU family (Intel x86-64 / AMD x86-64), emulator/hypervisor (e.g., QEMU, VirtualBox, VMware), or bare-metal hardware details.
* **Reproduction steps / Proof of Concept (PoC):** Minimal C++ source code, `.texe` binary steps, or exact syscall sequence that triggers the issue.
* **Impact assessment:** What an attacker or unprivileged process can achieve (e.g., Ring 0 code execution, kernel crash/DoS, unauthorized file read/write).

---

## Response & Disclosure Timeline

* **Initial Acknowledgment:** Within **3 business days** of receiving your report.
* **Triage & Severity Assessment:** Within **7 days**, including confirmation of whether the issue reproduces on `main`.
* **Patch & Coordinated Disclosure:** Once a fix is verified and merged into `main`, a public GitHub Security Advisory will be published crediting the reporter (unless anonymity is requested). We ask that you hold off on public disclosure until a patch is released or **90 days** have passed since initial report.
