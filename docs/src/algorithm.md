# Algorithm

The personal goal is to improve the MPI Reduce algorithm on the cluster.

### MPI Reduce




### MPI Scatter/Gather






### My debugging section

- Following code snippet is based on the implementation that Marcel has implemented and an unoptimized one, for personal use to compare


```bash
[dphpc@slimflysmw tuned]$ diff -y --suppress-common-lines coll_tuned_alltoall.c /home/dphpc/scratch/dphpc/wyou/openmpi/ompi/mca/coll/tuned/coll_tuned_alltoall.c 
/*                                                            <
arameter "coll_tuned_alltoall_algorithm" (current value: "ign <
Which alltoall algorithm is used. Can be locked down to choic <
Valid values:                                                 <
0:"ignore",                                                   <
1:"linear",                                                   <
2:"pairwise",                                                 <
3:"modified_bruck",                                           <
4:"linear_sync",                                              <
5:"two_proc"                                                  <
*/                                                            <
                                                              <
#include<stdio.h>                                             <
#include<stdlib.h>                                            <
                                                              <
void FisherYatesAllToAll(int * arr, unsigned int size, unsign <
    srand(seed);                                              <
    for(int i = size - 1; i > 0; --i){                        <
        int j = rand() % i;                                   <
        int tmp = arr[i];                                     <
        arr[i] = arr[j];                                      <
        arr[j] = tmp;                                         <
    }                                                         <
}                                                             <
                                                              <
int str2int(const char * s){                                  <
   char *ptr;                                                 <
   long ret;                                                  <
                                                              <
   ret = strtol(s, &ptr, 10);                                 <
   return (int) ret;                                          <
}                                                             <
                                                              <
                                                              <
                                                              <
                                                              <
    //printf("Ayo mandem using da custom inplace algorithm ye <
                                                              <
                                                              <
    int* idx = (int*) malloc(sizeof(unsigned int) * size);    <
                                                              <
    for(int j = 0; j < size; ++j){                            <
        idx[j] = j;                                           <
    }                                                         <
                                                              <
    FisherYatesAllToAll(idx, size, size);                     <
                                                              <
            if (idx[i] == rank) {                             |             if (i == rank) {
                                                       (char  |                                                        (char 
                err = MCA_PML_CALL(recv ((char *) rbuf + max_ |                 err = MCA_PML_CALL(irecv ((char *) rbuf + max
                                          idx[j], MCA_COLL_BA |                                           j, MCA_COLL_BASE_TA
                err = MCA_PML_CALL(send ((char *) tmp_buffer, |                 err = MCA_PML_CALL(isend ((char *) tmp_buffer
                                          idx[j], MCA_COLL_BA |                                           j, MCA_COLL_BASE_TA
                                          comm));//, preq++)) |                                           comm, preq++));
            } else if (idx[j] == rank) {                      |             } else if (j == rank) {
                                                       (char  |                                                        (char 
                err = MCA_PML_CALL(recv ((char *) rbuf + max_ |                 err = MCA_PML_CALL(irecv ((char *) rbuf + max
                                          idx[i], MCA_COLL_BA |                                           i, MCA_COLL_BASE_TA
                err = MCA_PML_CALL(send ((char *) tmp_buffer, |                 err = MCA_PML_CALL(isend ((char *) tmp_buffer
                                          idx[i], MCA_COLL_BA |                                           i, MCA_COLL_BASE_TA
                                          comm));//, preq++)) |                                           comm, preq++));
           // err = ompi_request_wait_all (2, tuned_module->t |             err = ompi_request_wait_all (2, tuned_module->tun
            //if (MPI_SUCCESS != err) { goto error_hndl; }    |             if (MPI_SUCCESS != err) { goto error_hndl; }
            //mca_coll_tuned_free_reqs(tuned_module->tuned_da |             mca_coll_tuned_free_reqs(tuned_module->tuned_data
                                                              <
                                                              <
    free(idx);                                                <
                                                              <
    char * env;                                               <
    int npernode = 0;                                         <
    int nperswitch = 0;                                       <
    int debug = 0;                                            <
    int mode = 0;                                             <
                                                              <
    env = getenv("SF_NPERNODE");                              <
    if(env){npernode = str2int(env);}                         <
                                                              <
    env = getenv("SF_NPERSWITCH");                            <
    if(env){nperswitch = str2int(env);}                       <
                                                              <
    env = getenv("SF_DEBUG");                                 <
    if(env){debug = str2int(env);}                            <
                                                              <
    env = getenv("SF_ALLTOALL_MODE");                         <
    if(env){mode = str2int(env);}                             <
                                                              <
    if(mode == 0){                                            <
        goto base;                                            <
    } else if (mode == 1){                                    <
        goto random_scheduling;                               <
    } else if (mode == 2) {                                   <
        goto custom_scheduling;                               <
    } else if (mode == 3){                                    <
        goto random_grid;                                     <
    } else {                                                  <
        goto err_hndl;                                        <
    }                                                         <
                                                              <
    if(debug){                                                <
        printf("RANK: %i\nnpernode=%i\nnperswitch=%i\nmode=%i <
    }                                                         <
                                                              <
    // DEFAULT                                                <
  base:                                                       <
    if(debug == 3){                                           <
        printf("Using default scheduling.\n");                <
    }                                                         <
                                                              <
        if(debug){                                            <
            printf("Rank %i: sending to %i, receiving from %i <
        }                                                     <
                                                              <
                                                              <
                                                              |  
    // RANDOM SCHEDULING                                      <
 random_scheduling:                                           <
                                                              <
    if(debug == 3){                                           <
        printf("Using random scheduling.\n");                 <
    }                                                         <
                                                              <
    int* idx = (int*) malloc(sizeof(int) * size);             <
                                                              <
    for(int j = 0; j < size; ++j){                            <
        idx[j] = j+1;                                         <
    }                                                         <
                                                              <
    FisherYatesAllToAll(idx, size-1, size);                   <
                                                              <
    /* Perform pairwise exchange - starting from 1 so the loc <
    for (int i = 0; i < size; ++i) {                          <
        step = idx[i];                                        <
        /* Determine sender and receiver for this step. */    <
                                                              <
        sendto  = (rank + step) % size;                       <
        recvfrom = (rank + size - step) % size;               <
                                                              <
        if(debug){                                            <
            printf("Rank %i: sending to %i, receiving from %i <
        }                                                     <
                                                              <
        /* Determine sending and receiving locations */       <
        tmpsend = (char*)sbuf + (ptrdiff_t)sendto * sext * (p <
        tmprecv = (char*)rbuf + (ptrdiff_t)recvfrom * rext *  <
                                                              <
        /* send and receive */                                <
        err = ompi_coll_tuned_sendrecv( tmpsend, scount, sdty <
                                        MCA_COLL_BASE_TAG_ALL <
                                        tmprecv, rcount, rdty <
                                        MCA_COLL_BASE_TAG_ALL <
                                        comm, MPI_STATUS_IGNO <
        if (err != MPI_SUCCESS) { line = __LINE__; goto err_h <
    }                                                         <
    free(idx);                                                <
    return MPI_SUCCESS;                                       <
                                                              <
    // CUSTOM SCHEDULING                                      <
 custom_scheduling:                                           <
                                                              <
    if(debug == 3){                                           <
        printf("Using custom scheduling.\n");                 <
    }                                                         <
                                                              <
    int switches = size / nperswitch;                         <
                                                              <
                                                              <
    /* Perform pairwise exchange - starting from 1 so the loc <
    for (int i = 0; i < nperswitch; ++i){                     <
        for (int j = 0; j < switches; ++j) {                  <
            step = nperswitch*j + i;                          <
                                                              <
            if(step == 0 || step > size){continue;}           <
                                                              <
            /* Determine sender and receiver for this step. * <
            sendto  = (rank + step) % size;                   <
            recvfrom = (rank + size - step) % size;           <
                                                              <
            if(debug){                                        <
                printf("Rank %i: sending to %i, receiving fro <
            }                                                 <
                                                              <
                                                              <
            /* Determine sending and receiving locations */   <
            tmpsend = (char*)sbuf + (ptrdiff_t)sendto * sext  <
            tmprecv = (char*)rbuf + (ptrdiff_t)recvfrom * rex <
                                                              <
            /* send and receive */                            <
            err = ompi_coll_tuned_sendrecv( tmpsend, scount,  <
                                            MCA_COLL_BASE_TAG <
                                            tmprecv, rcount,  <
                                            MCA_COLL_BASE_TAG <
                                            comm, MPI_STATUS_ <
            if (err != MPI_SUCCESS) { line = __LINE__; goto e <
        }                                                     <
    }                                                         <
                                                              <
    // Do last local copy                                     <
    /* Determine sender and receiver for this step. */        <
    sendto  = (rank) % size;                                  <
    recvfrom = (rank + size) % size;                          <
                                                              <
    /* Determine sending and receiving locations */           <
    tmpsend = (char*)sbuf + (ptrdiff_t)sendto * sext * (ptrdi <
    tmprecv = (char*)rbuf + (ptrdiff_t)recvfrom * rext * (ptr <
                                                              <
    /* send and receive */                                    <
    err = ompi_coll_tuned_sendrecv( tmpsend, scount, sdtype,  <
                                    MCA_COLL_BASE_TAG_ALLTOAL <
                                    tmprecv, rcount, rdtype,  <
                                    MCA_COLL_BASE_TAG_ALLTOAL <
                                    comm, MPI_STATUS_IGNORE,  <
    if (err != MPI_SUCCESS) { line = __LINE__; goto err_hndl; <
                                                              <
    return MPI_SUCCESS;                                       <
 random_grid:                                                 <
    if(debug == 3){                                           <
        printf("Using random grid scheduling.\n");            <
    }                                                         <
                                                              <
    // Allocate grid                                          <
    idx = (int*) malloc(sizeof(int) * size * size);           <
                                                              <
    for(int i = 0; i < size; ++i){                            <
        for(int j = 0; j < size; ++j){                        <
            if(j < i){                                        <
                idx[i*size + j] = -1;                         <
            } else {                                          <
                idx[i*size + j] = j;                          <
            }                                                 <
        }                                                     <
                                                              <
        FisherYatesAllToAll(idx + i*size, size, size + i);    <
    }                                                         <
                                                              <
                                                              <
                                                              <
    /* Perform pairwise exchange - starting from 1 so the loc <
                                                              <
    for (int j = 0; j < size; ++j) {                          <
        for (int i = 0; i < size; ++i) {                      <
                                                              <
            /* Determine sender and receiver for this step. * <
            // sendto  = (rank + step) % size;                <
            // recvfrom = (rank + size - step) % size;        <
            if(idx[i*size + j]==-1){continue;}                <
                                                              <
            if(rank != i && rank != idx[i*size + j]){continue <
            else if(rank == i){                               <
                sendto = idx[i*size + j];                     <
                recvfrom = idx[i*size + j];                   <
            } else {                                          <
                sendto = i;                                   <
                recvfrom = i;                                 <
            }                                                 <
                                                              <
                                                              <
            if(debug){                                        <
                printf("Rank %i: sending to %i, receiving fro <
            }                                                 <
                                                              <
                                                              <
            /* Determine sending and receiving locations */   <
            tmpsend = (char*)sbuf + (ptrdiff_t)sendto * sext  <
            tmprecv = (char*)rbuf + (ptrdiff_t)recvfrom * rex <
                                                              <
            /* send and receive */                            <
            err = ompi_coll_tuned_sendrecv( tmpsend, scount,  <
                                            MCA_COLL_BASE_TAG <
                                            tmprecv, rcount,  <
                                            MCA_COLL_BASE_TAG <
                                            comm, MPI_STATUS_ <
            if (err != MPI_SUCCESS) { line = __LINE__; goto e <
                                                              <
        }                                                     <
    }                                                         <
                                                              <
    free(idx);                                                <
    return MPI_SUCCESS;                                       <
                                                              <
    char * env;                                               <
    int debug = 0;                                            <
                                                              <
    env = getenv("SF_DEBUG");                                 <
    if(env){debug = str2int(env);}                            <
                                                              <
    if(debug){                                                <
        printf("Using custom Brucks alltoall!\n");            <
    }                                                         <
                                                              <
                                                              <
        if(debug){                                            <
            printf("Delegating to pasic inplace!\n");         <
        }                                                     <
                                                              <
    if(debug){                                                <
        printf("Running modified bruck!\n");                  <
    }                                                         <
                                                              <
    // Randomize                                              <
    int* idx = (int*) malloc(sizeof(unsigned int) * size);    <
                                                              <
    int len = 0;                                              <
    for(int j = 1; j < size; j<<=1, ++len){                   <
        idx[len] = j;                                         <
    }                                                         <
                                                              <
    FisherYatesAllToAll(idx, len, size);                      <
                                                              <
                                                              <
    for (int d = 0; d < len; ++d) {                           |     for (distance = 1; distance < size; distance<<=1) {
        distance = idx[d];                                    |



```
