# WSK Client (wskclient)

This library provides a convenience wrapper around the [WSK API](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/introduction-to-winsock-kernel), hiding cumbersome details like NMR, IRP and MDL management. The common socket operations are provided in both synchronous and asynchronous flavors.

## API

Test code can leverage the API by including [wskclient.h](../inc/wskclient.h) and linking against wskclient.lib. Kernel-mode only.
