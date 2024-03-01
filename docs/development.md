# How to develop

## Get the code

Clone this repo and ensure all submodules are cloned with the `--recursive` option:

```
git clone https://github.com/microsoft/win-net-test.git --recursive
```

Or, if the repo was already cloned nonrecursively:

```
git submodule update --init --recursive
```

## Build the code

### Prerequisites

- [Visual Studio](https://visualstudio.microsoft.com/downloads/)
  - Visual Studio 2022 is recommended; Visual Studio 2019 or newer is required.
  - Latest Spectre-mitigated libs (via "Individual components" section of Visual Studio Installer)
- [Windows Driver Kit](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
  - WDK for Windows 11, version 22H2 (version 10.0.22621.x) is recommended; WDK for Windows Server 2022 LTSC or newer is required.

### Building

Run in a Visual Studio "Developer Command Prompt":

```PowerShell
.\tools\build.ps1
```

## Test the code

The test machine must have the "artifacts" and "tools" directories from the repo, either
by cloning the repo and building the code or by copying them from another system. The
file layout is assumed to be identical to that of the repo.

### Running functional tests

One-time setup:

```Powershell
.\tools\prepare-machine.ps1 -ForFunctionalTest
```

Running the tests:

```Powershell
.\tools\functional.ps1
```

Querying the list of test cases:

```Powershell
.\tools\functional.ps1 -ListTestCases
```

Running a specific test case:

```Powershell
.\tools\functional.ps1 -TestCaseFilter "Name=MpBasicRx"
```

After the test, convert the logs:

```Powershell
.\tools\log.ps1 -Convert -Name fnfunc
```
