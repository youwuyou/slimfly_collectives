/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#include "ompi_config.h"
#include "coll_basic.h"

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "coll_basic.h"
#include "ompi/mca/pml/pml.h"
#include "opal/util/bit_ops.h"
#include <stdlib.h>
#include <stdio.h>

// 1 4 2 3 0
// a b c d e

void FisherYatesBcast(int * arr, int size){
    for(int i = size - 1; i > 0; --i){
        int j = rand() % i;
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

void ReshuffleReqs(ompi_request_t** arr, int * idx, int size){
    
    ompi_request_t** tmp = (ompi_request_t**) malloc(sizeof(ompi_request_t*) * size);

    for(int i = 0; i < size; ++i){
        tmp[i] = arr[idx[i]];
    }

    for(int i = 0; i < size; ++i){
        arr[i] = tmp[i];
    }

    free(tmp);
}



/*
 *	bcast_lin_intra
 *
 *	Function:	- broadcast using O(N) algorithm
 *	Accepts:	- same arguments as MPI_Bcast()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_basic_bcast_lin_intra(void *buff, int count,
                               struct ompi_datatype_t *datatype, int root,
                               struct ompi_communicator_t *comm,
                               mca_coll_base_module_t *module)
{
    /* 
    Base Performance:
    # OSU MPI Broadcast Latency Test v6.2
    # Size       Avg Latency(us)
    1                       3.42
    2                       3.24
    4                       2.49
    8                       2.60
    16                      2.61
    32                      2.68
    64                      2.73
    128                     3.00
    256                     3.28
    512                     3.48
    1024                    3.93
    2048                    5.08
    4096                    6.70
    8192                    9.73
    16384                  34.54
    32768                  62.13
    65536                  88.28
    131072                160.28
    262144                297.32
    524288                599.51
    1048576              1147.56
    2097152              2286.14
    4194304              4560.53

    Modified Performance:
    # OSU MPI Broadcast Latency Test v6.2
    # Size       Avg Latency(us)
    1                       3.74
    2                       3.52
    4                       2.75
    8                       2.83
    16                      2.85
    32                      2.94
    64                      3.00
    128                     3.39
    256                     3.65
    512                     3.89
    1024                    4.32 
    2048                    5.48
    4096                    7.20
    8192                   10.40
    16384                  36.24
    32768                  63.87
    65536                  91.60
    131072                158.77
    262144                301.22
    524288                579.61
    1048576              1167.14
    2097152              2285.16
    4194304              4560.62

    */
    int i;
    int size;
    int rank;
    int err;
    ompi_request_t **preq;
    mca_coll_basic_module_t *basic_module = (mca_coll_basic_module_t*) module;
    ompi_request_t **reqs = basic_module->mccb_reqs;
    // printf("NUM REQUESTS %i", basic_module->mccb_num_reqs);
    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);

    /* Non-root receive the data. */

    if (rank != root) {
        return MCA_PML_CALL(recv(buff, count, datatype, root,
                                 MCA_COLL_BASE_TAG_BCAST, comm,
                                 MPI_STATUS_IGNORE));
    }

    /* Root sends data to all others. */
    // Shuffle indices
    int* idx = (int*) malloc(sizeof(int) * size);
 
    // Fill array  
    // printf("Filling array\n");
    for(int j = 0; j < size; ++j){
        idx[j] = j;
    }

    // Shuffle array randomly
    // printf("Starting Yates\n");
    FisherYatesBcast(idx, size);
    // printf("Done Yates\n");


    for (i = 0, preq = reqs; i < size; ++i) {
        if (idx[i] == rank) {
            // printf("Root found\n");
            continue;
        }

        // Bcast to random process
        // printf("Sending\n");
        err = MCA_PML_CALL(isend_init(buff, count, datatype, idx[i],
                                      MCA_COLL_BASE_TAG_BCAST,
                                      MCA_PML_BASE_SEND_STANDARD,
                                      comm, preq++));
        if (MPI_SUCCESS != err) {
            return err;
        }
    }

    // Reshuffle request
    // printf("NUM REQUESTS %i", basic_module->mccb_num_reqs);
    // printf("Starting shuffle\n");
    //ReshuffleReqs(reqs, idx, size);
    // printf("Done shuffle\n");
    // printf("NUM REQUESTS %i", basic_module->mccb_num_reqs);
    // free idx
    free(idx);

    --i;

    /* Start your engines.  This will never return an error. */
    // printf("Starting engines\n");
    MCA_PML_CALL(start(i, reqs));

    /* Wait for them all.  If there's an error, note that we don't
     * care what the error was -- just that there *was* an error.  The
     * PML will finish all requests, even if one or more of them fail.
     * i.e., by the end of this call, all the requests are free-able.
     * So free them anyway -- even if there was an error, and return
     * the error after we free everything. */
    // printf("Wait\n");
    err = ompi_request_wait_all(i, reqs, MPI_STATUSES_IGNORE);

    /* Free the reqs */
    // printf("Freeing requests like the slave kids in Indiana Jones and the temple of doom.\n");
    mca_coll_basic_free_reqs(reqs, i);

    
    /* All done */

    return err;
}


/*
 *	bcast_log_intra
 *
 *	Function:	- broadcast using O(log(N)) algorithm
 *	Accepts:	- same arguments as MPI_Bcast()
 *	Returns:	- MPI_SUCCESS or error code
 */

/*
    Base performance:
    # OSU MPI Broadcast Latency Test v6.2
    # Size       Avg Latency(us)
    1                       1.53
    2                       1.45
    4                       1.13
    8                       1.19
    16                      1.21
    32                      1.27
    64                      1.36
    128                     2.29
    256                     2.58
    512                     2.82
    1024                    3.43
    2048                    4.62
    4096                    5.98
    8192                    8.55
    16384                  27.96
    32768                  41.66
    65536                  64.83
    131072                118.44
    262144                240.07
    524288                400.14
    1048576               799.19
    2097152              1588.14
    4194304              3168.61
*/

int
mca_coll_basic_bcast_log_intra(void *buff, int count,
                               struct ompi_datatype_t *datatype, int root,
                               struct ompi_communicator_t *comm,
                               mca_coll_base_module_t *module)
{
    int i;
    int size;
    int rank;
    int vrank;
    int peer;
    int dim;
    int hibit;
    int mask;
    int err;
    int nreqs;
    ompi_request_t **preq;
    mca_coll_basic_module_t *basic_module = (mca_coll_basic_module_t*) module;
    ompi_request_t **reqs = basic_module->mccb_reqs;

    size = ompi_comm_size(comm);
    rank = ompi_comm_rank(comm);
    vrank = (rank + size - root) % size;

    dim = comm->c_cube_dim;
    hibit = opal_hibit(vrank, dim);
    --dim;

    /* Receive data from parent in the tree. */

    if (vrank > 0) {
        peer = ((vrank & ~(1 << hibit)) + root) % size;

        err = MCA_PML_CALL(recv(buff, count, datatype, peer,
                                MCA_COLL_BASE_TAG_BCAST,
                                comm, MPI_STATUS_IGNORE));
        if (MPI_SUCCESS != err) {
            return err;
        }
    }

    /* Send data to the children. */

    err = MPI_SUCCESS;
    preq = reqs;
    nreqs = 0;
    for (i = hibit + 1, mask = 1 << i; i <= dim; ++i, mask <<= 1) {
        peer = vrank | mask;
        if (peer < size) {
            peer = (peer + root) % size;
            ++nreqs;

            err = MCA_PML_CALL(isend_init(buff, count, datatype, peer,
                                          MCA_COLL_BASE_TAG_BCAST,
                                          MCA_PML_BASE_SEND_STANDARD,
                                          comm, preq++));
            if (MPI_SUCCESS != err) {
                mca_coll_basic_free_reqs(reqs, nreqs);
                return err;
            }
        }
    }

    /* Start and wait on all requests. */

    if (nreqs > 0) {

        /* Start your engines.  This will never return an error. */

        MCA_PML_CALL(start(nreqs, reqs));

        /* Wait for them all.  If there's an error, note that we don't
         * care what the error was -- just that there *was* an error.
         * The PML will finish all requests, even if one or more of them
         * fail.  i.e., by the end of this call, all the requests are
         * free-able.  So free them anyway -- even if there was an
         * error, and return the error after we free everything. */

        err = ompi_request_wait_all(nreqs, reqs, MPI_STATUSES_IGNORE);

        /* Free the reqs */

        mca_coll_basic_free_reqs(reqs, nreqs);
    }

    /* All done */

    return err;
}


/*
 *	bcast_lin_inter
 *
 *	Function:	- broadcast using O(N) algorithm
 *	Accepts:	- same arguments as MPI_Bcast()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_basic_bcast_lin_inter(void *buff, int count,
                               struct ompi_datatype_t *datatype, int root,
                               struct ompi_communicator_t *comm,
                               mca_coll_base_module_t *module)
{
    int i;
    int rsize;
    int err;
    mca_coll_basic_module_t *basic_module = (mca_coll_basic_module_t*) module;
    ompi_request_t **reqs = basic_module->mccb_reqs;

    rsize = ompi_comm_remote_size(comm);

    if (MPI_PROC_NULL == root) {
        /* do nothing */
        err = OMPI_SUCCESS;
    } else if (MPI_ROOT != root) {
        /* Non-root receive the data. */
        err = MCA_PML_CALL(recv(buff, count, datatype, root,
                                MCA_COLL_BASE_TAG_BCAST, comm,
                                MPI_STATUS_IGNORE));
    } else {
        /* root section */
        for (i = 0; i < rsize; i++) {
            err = MCA_PML_CALL(isend(buff, count, datatype, i,
                                     MCA_COLL_BASE_TAG_BCAST,
                                     MCA_PML_BASE_SEND_STANDARD,
                                     comm, &(reqs[i])));
            if (OMPI_SUCCESS != err) {
                return err;
            }
        }
        err = ompi_request_wait_all(rsize, reqs, MPI_STATUSES_IGNORE);
    }


    /* All done */
    return err;
}


/*
 *	bcast_log_inter
 *
 *	Function:	- broadcast using O(N) algorithm
 *	Accepts:	- same arguments as MPI_Bcast()
 *	Returns:	- MPI_SUCCESS or error code
 */
int
mca_coll_basic_bcast_log_inter(void *buff, int count,
                               struct ompi_datatype_t *datatype, int root,
                               struct ompi_communicator_t *comm,
                               mca_coll_base_module_t *module)
{
    return OMPI_ERR_NOT_IMPLEMENTED;
}
