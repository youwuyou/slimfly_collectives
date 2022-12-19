# Performance


## Prediction models

All of the following models can provide useful insights into various aspects of different algorithms and their relative performances (source: [Pjesivac-Grbovic et al.](https://ieeexplore.ieee.org/document/1420226))

- Hockney, LogP/LogGP, and PLogP







## Performance analysis of MPI collective operations

- 













---

The personal goal is to improve the MPI Reduce algorithm on the cluster. The following section contains the results using the algorithms from the `tuned` module of Open MPI


### `osu_reduce`

> Followingly we run the benchmarks on 200 nodes performing 100 iterations for which selected size, and use `osu_micro_benchmarks` to measure the performance of the system. We choose the algorithms using the `-mca` switch and specify the wanted algorithms by `-mca coll_tuned_use_dynamic_rules 1 -mca coll_tuned_reduce_algorithm [0~6]` where the number of the already implemented algorithms varies from 0 to 6

```bash
# the passed flags of the following command does the following
# i). makes sure that we are running the program using IB
# ii). enable dynamic choice of the algorithm to be performed given a specific number
$(which mpirun) -x PATH=$PATH -x LD_LIBRARY_PATH=$LD_LIBRARY_PATH -mca btl self,openib -mca btl_openib_allow_ib 1 -mca btl_openib_if_include mlx4_0 -mca btl_openib_ib_path_selection_strategy 1 -mca btl_openib_max_lmc 0 -mca btl_openib_enable_apm_over_lmc 0 -mca btl_openib_btls_per_lid 1 -mca pml bfo -mca btl_openib_ib_path_record_service_level 1 -mca coll_tuned_use_dynamic_rules 1 -mca coll_tuned_reduce_algorithm 6 --npernode 1 --hostfile ./200nodes osu_reduce -f -m 100000:5242880 -i 100 -M 50000000
```


The algorithms we can choose are the following

```C
/* valid values for coll_tuned_reduce_forced_algorithm */
static mca_base_var_enum_value_t reduce_algorithms[] = {
    {0, "ignore"},
    {1, "linear"},
    {2, "chain"},
    {3, "pipeline"},
    {4, "binary"},
    {5, "binomial"},
    {6, "in-order_binary"},
    {0, NULL}
};
```

**Algorithm 0: ignore**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000                132.14             31.23            831.04         100
200000                322.67             62.81           1563.97         100
400000               2834.61            146.85           3965.61         100
800000               8297.08            324.86           9236.95         100
1600000             35948.33            736.15          37948.87         100
3200000            276274.93           1630.58         281278.16         100
```

**Algorithm 1: linear**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000               6487.39            443.43          12355.04         100
200000               9448.29            552.40          18120.21         100
400000              16387.96            600.33          31946.03         100
800000              30068.94            666.30          59422.85         100
1600000             58182.33            946.41         115545.30         100
3200000            113780.66           1140.69         227546.26         100
```

**Algorithm 2: chain**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000               1666.99             42.11           3464.73         100
200000               5448.95            177.72          11015.41         100
400000               8196.34            260.80          16709.97         100
800000              13808.33            420.40          28388.52         100
1600000             25167.34            703.10          51974.58         100
3200000             45377.38           1069.22          93009.15         100
```

**Algorithm 3: pipeline**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000               6712.54             57.09          13386.94         100
200000              21579.71            198.61          42843.28         100
400000              32205.14            291.74          64115.23         100
800000              54162.76            485.81         107843.99         100
1600000             99080.83            879.45         197513.47         100
3200000            178693.16           1389.62         355750.96         100
```

**Algorithm 4: binary**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000                229.67             41.72            879.07         100
200000                706.75            178.87           2515.09         100
400000               1144.38            257.11           4142.77         100
800000               1973.52            406.54           6808.06         100
1600000              3666.60            718.00          12641.29         100
3200000              6484.51           1079.66          23392.05         100
```

**Algorithm 5: binomial**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000                122.61             39.89            609.98         100
200000                396.09            160.07           1198.21         100
400000                584.20            212.95           1904.71         100
800000               1006.74            332.68           3487.79         100
1600000              1840.96            551.60           6741.70         100
3200000              3161.08            946.51          12395.23         100
```

**Algorithm 6: in-order_binary**
```bash
# OSU MPI Reduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000                176.08             40.90            914.90         100
200000                790.12            187.62           3052.36         100
400000                911.30            247.37           3559.09         100
800000               1510.80            390.38           5900.00         100
1600000              2820.92            701.73          11307.00         100
3200000              5039.58           1072.93          20802.54         100
```



### `osu_allreduce`

> We launched the osu-benchmarks using the following argument, choosing the desired allreduce algorithms `$(which mpirun) -x PATH=$PATH -x LD_LIBRARY_PATH=$LD_LIBRARY_PATH -mca btl self,openib -mca btl_openib_allow_ib 1 -mca btl_openib_if_include mlx4_0 -mca btl_openib_ib_path_selection_strategy 1 -mca btl_openib_max_lmc 0 -mca btl_openib_enable_apm_over_lmc 0 -mca btl_openib_btls_per_lid 1 -mca pml bfo -mca btl_openib_ib_path_record_service_level 1 -mca coll_tuned_use_dynamic_rules 1 -mca coll_tuned_allreduce_algorithm 1 --npernode 1 --hostfile ./200nodes osu_allreduce -f -m 100000:5242880 -i 100 -M 50000000`

```C
# avaliable existing algorithms
static mca_base_var_enum_value_t allreduce_algorithms[] = {
    {0, "ignore"},
    {1, "basic_linear"},
    {2, "nonoverlapping"},
    {3, "recursive_doubling"},
    {4, "ring"},
    {5, "segmented_ring"},
    {0, NULL}
};
```


**Algorithm 0: ignore**
```bash
# OSU MPI Allreduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000               1181.07           1168.71           1188.92         100
200000               1462.62           1442.96           1479.07         100
400000               1818.38           1803.78           1834.10         100
800000               3303.89           3186.16           3449.73         100
1600000              3171.33           3154.79           3184.00         100
3200000              9006.89           8725.43           9220.24         100
```

**Algorithm 1: basic_linear**
```bash
# OSU MPI Allreduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000              16879.04          16450.94          17293.41         100
200000              26054.80          25808.69          26254.45         100
400000              43064.00          39144.78          45117.78         100
800000              82261.62          70034.41          84763.74         100
1600000            162705.45         148548.90         165350.29         100
3200000            323221.61         308570.94         326295.46         100
```

**Algorithm 2: nonoverlapping**
```bash
# OSU MPI Allreduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000               1163.39            938.96           1247.31         100
200000               1752.41           1531.61           1793.60         100
400000               4019.97           3017.85           4804.85         100
800000              11037.44           9612.77          11960.02         100
1600000             39830.66          37595.83          40870.52         100
3200000            325071.04         318648.53         327393.94         100
```

**Algorithm 3: recursive_doubling**
```bash
# OSU MPI Allreduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000                848.37            774.12            914.16         100
200000               1581.94           1467.31           1702.09         100
400000               2763.76           2534.04           3069.25         100
800000               5172.03           4515.14           5691.36         100
1600000              9693.43           8079.93          10726.02         100
3200000             18994.18          15796.30          20934.79         100
```

**Algorithm 4: ring**
```bash
# OSU MPI Allreduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000                851.32            779.69            908.17         100
200000               3113.09           2909.20           3333.70         100
400000               2611.02           2342.51           2907.94         100
800000               5028.45           4326.89           5594.54         100
1600000              9659.68           7959.01          10758.98         100
3200000             19051.46          15704.57          20881.06         100
```

**Algorithm 5: segmented_ring**
```bash
# OSU MPI Allreduce Latency Test v6.2
# Size       Avg Latency(us)   Min Latency(us)   Max Latency(us)  Iterations
100000               1174.00           1165.55           1182.63         100
200000               1452.26           1434.70           1476.29         100
400000               1782.82           1770.29           1792.72         100
800000               3295.72           3154.78           3411.68         100
1600000              3181.24           3150.86           3202.50         100
3200000              8853.35           8709.90           9026.74         100
```




`osu_get_latency`
```bash

```

`osu_latency`
```bash

```

`osu_latency_mp`
```bash

```


`osu_latency_mt`
```bash

```

`osu_alltoall`
```bash

```


`osu_alltoallv`
```bash


```

`osu_alltoallw`
```bash


```
