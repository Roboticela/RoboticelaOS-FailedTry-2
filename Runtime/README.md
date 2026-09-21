# Roboticela Runtime

Independent C runtime for Roboticela OS.

All architecture, API and design documents live in **[Documentation/Runtime/](../Documentation/Runtime/ARCHITECTURE.md)**. This folder holds source only.

## Build and test

```powershell
.\Scripts\Build\BuildRuntime.ps1
.\Scripts\Test\TestRuntime.ps1 -Target Host
```

One module at a time (does not rebuild the whole tree):

```powershell
.\Scripts\Build\BuildHtml.ps1
.\Scripts\Test\TestHtml.ps1 -Target Host
```
