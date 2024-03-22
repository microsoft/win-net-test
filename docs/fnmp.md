# Functional Miniport (FnMp)

This tool provides an [NDIS miniport driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/ndis-miniport-drivers2)
that is used as a hook into the lowest part of the [NDIS networking stack](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/ndis-driver-stack).

## API

Test code can interact with the miniport using the [fnmpapi](../inc/fnmpapi.h).
Hooks for both the NDIS data path and the NDIS control are supported.
