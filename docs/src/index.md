# Slim Fly MPI Collective Optimization

The goal of the project is to optimize the MPI collective used on an existing cluster whose underlying network topology is based on the Slim Fly network topology. 

[Source: Slim Fly: A Cost Effective Low-Diameter
Network Topology (Maciej Besta, Torsten Hoefler)](https://arxiv.org/pdf/1912.08968.pdf)

# Getting Started

The current MPI version we are using is OpenMPI of version 1.10.7.

```bash
# by using mpiexec --version

mpiexec (OpenRTE) 1.10.7
```


```bash
# Activate the spack env in /home/dphpc/scratch/dphpc/wyou
spacktivate .

# Go into wyou/openmpi/ompi/mca/coll/tuned/ here you find the collectives
cd openmpi/ompi/mca/coll/tuned

# Modify the collective and spack install again (we should find a way to not reinstall everything every time because it takes forever)


# In wyou/benchmarks/ there are some configs (mainly hostfiles) for testing. The main command to test a collective is this one (using osu microbenchmarks):

$(which mpirun) -x PATH=$PATH -x LD_LIBRARY_PATH=$LD_LIBRARY_PATH -mca btl self,openib -mca btl_openib_allow_ib 1 -mca btl_openib_if_include mlx4_0 -mca btl_openib_ib_path_selection_strategy 0 -mca btl_openib_max_lmc 0 -mca btl_openib_enable_apm_over_lmc 0 -mca btl_openib_btls_per_lid 1 -mca pml bfo -mca btl_openib_ib_path_record_service_level 1 -mca coll ^tuned -mca coll_basic_priority 100000 -mca coll_basic_crossover 100 -np 8 --npernode 1 --hostfile ./8nodes osu_bcast -m 100000:5242880 -i 1
```

## Host file

> The host file contains names of all of the computers on which the MPI job will execute. Either use the flag `--hostfile ./host_file` or setting the environment variable called `MPI_HOSTS` to specify.