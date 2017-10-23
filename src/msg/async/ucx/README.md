# UCX CEPH

## Useful commands

- How to run using UCX

``` bash
  MDS=0 MON=1 OSD=3 ./src/vstart.sh -o "ms_type=async+ucx" -o "ms_async_ucx_device=mlx5_0:1" -n -i 12.210.8.58
```

- gtest

``` bash
  env UCX_IB_ETH_PAUSE_ON=y bin/ceph_test_msgr --gtest_filter=*SimpleTest/2
```
