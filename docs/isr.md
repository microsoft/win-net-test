# Invoke System Relay (Isr)

User-mode tests sometimes take a dependency on the user-mode only system() API. When porting these tests to kernel-mode, there is no straightforward translation.
This tool provides a system()-like kernel-mode API that relays commands to user-mode, executes them, then returns the result.

## API

Kernel-mode test code can interact with the API using [invokesystemrelay.h](../inc/invokesystemrelay.h).
