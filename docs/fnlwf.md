# Functional Lightweight Filter (FnLwf)

This tool provides an [NDIS filter driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/ndis-filter-drivers)
that is used as a hook into the middle of the [NDIS networking stack](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/ndis-driver-stack).

## API

Test code can interact with the filter using the [fnlwfapi](./inc/fnlwfapi.h).
Hooks for both the NDIS data path and the NDIS control are supported.
