#!/bin/bash
NODES=${1:-32}
SIZE_BYTES=${2:-1024}
ITERATIONS=${3:-10}
TIMEOUT=${4:-15m}

#LOG=/scratch/2/t2hx/alltoall/out/"$(date +"%F_%T")".out
#echo "Writing to "${LOG}
/scratch/2/t2hx/alltoall/generate_alltoall_schedules.sh ${NODES}
#TIME=$(timeout --kill-after=30s ${TIMEOUT} /scratch/2/t2hx/dep/adaptive_openmpi/bin/mpirun -mca plm_rsh_no_tree_spawn 1 --map-by node -x OMP_NUM_THREADS=1 -x OMP_PROC_BIND=spread -mca btl self,openib -mca btl_openib_allow_ib 1 -mca btl_openib_if_include mlx4_0 --hostfile /scratch/2/t2hx/conf/hosts.linear.${NODES} -mca orte_base_help_aggregate 0 -mca btl_openib_max_lmc 0 -mca btl_openib_enable_apm_over_lmc 0 -mca btl_openib_btls_per_lid 1 -mca pml bfo -np ${NODES} -mca btl_openib_ib_path_record_service_level 1 /scratch/2/t2hx/alltoall/a.out ${SIZE_BYTES} ${ITERATIONS} | sort -n  | tail -n 1 | cut -c 12-)
TIME=$(timeout --kill-after=30s ${TIMEOUT} /scratch/2/t2hx/dep/adaptive_openmpi/bin/mpirun -mca plm_rsh_no_tree_spawn 1 --map-by node -x OMP_NUM_THREADS=1 -x OMP_PROC_BIND=spread -mca btl self,openib -mca btl_openib_allow_ib 1 -mca btl_openib_if_include mlx4_0 --hostfile /scratch/2/t2hx/conf/hosts.debug.${NODES} -mca orte_base_help_aggregate 0 -mca btl_openib_max_lmc 0 -mca btl_openib_enable_apm_over_lmc 0 -mca btl_openib_btls_per_lid 1 -mca pml bfo -np ${NODES} -mca btl_openib_ib_path_record_service_level 1 /scratch/2/t2hx/alltoall/a.out ${SIZE_BYTES} ${ITERATIONS} | sort -n  | tail -n 1 | cut -c 12-)
echo "${NODES} ${SIZE_BYTES} ${ITERATIONS} ${TIME}" #> ${LOG}
