/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2014 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2013      Los Alamos National Security, LLC. All Rights
 *                         reserved.
 * Copyright (c) 2015      Intel, Inc. All rights reserved.
 * Copyright (c) 2015-2016 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include "mpi.h"
#include "ompi/constants.h"
#include "ompi/datatype/ompi_datatype.h"
#include "ompi/communicator/communicator.h"
#include "ompi/mca/coll/coll.h"
#include "ompi/mca/coll/base/coll_tags.h"
#include "ompi/mca/pml/pml.h"
#include "ompi/op/op.h"
#include "coll_tuned.h"
#include "coll_tuned_topo.h"

/* reduce algorithm variables */
static int coll_tuned_reduce_algorithm_count = 6;
static int coll_tuned_reduce_forced_algorithm = 0;
static int coll_tuned_reduce_segment_size = 0;
static int coll_tuned_reduce_max_requests;
static int coll_tuned_reduce_tree_fanout;
static int coll_tuned_reduce_chain_fanout;

/* valid values for coll_tuned_reduce_forced_algorithm */
static mca_base_var_enum_value_t reduce_algorithms[] = {
    {0, "ignore"},
    {1, "linear"},
    {2, "chain"},
    {3, "pipeline"},
    {4, "binary"},
    {5, "binomial"},
    {6, "in-order_binary"},
    {7, "rabenseifner"}
    {0, NULL}
};

/**
 * This is a generic implementation of the reduce protocol. It used the tree
 * provided as an argument and execute all operations using a segment of
 * count times a datatype.
 * For the last communication it will update the count in order to limit
 * the number of datatype to the original count (original_count)
 *
 * Note that for non-commutative operations we cannot save memory copy
 * for the first block: thus we must copy sendbuf to accumbuf on intermediate 
 * to keep the optimized loop happy.
 */
int ompi_coll_tuned_reduce_generic( void* sendbuf, void* recvbuf, int original_count,
                                    ompi_datatype_t* datatype, ompi_op_t* op,
                                    int root, ompi_communicator_t* comm,
                                    mca_coll_base_module_t *module,
                                    ompi_coll_tree_t* tree, int count_by_segment,
                                    int max_outstanding_reqs )
{
    char *inbuf[2] = {NULL, NULL}, *inbuf_free[2] = {NULL, NULL};
    char *accumbuf = NULL, *accumbuf_free = NULL;
    char *local_op_buffer = NULL, *sendtmpbuf = NULL;
    ptrdiff_t extent, size, gap, segment_increment;
    ompi_request_t* reqs[2] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL};
    int num_segments, line, ret, segindex, i, rank;
    int recvcount, prevcount, inbi;

    /**
     * Determine number of segments and number of elements
     * sent per operation
     */
    ompi_datatype_type_extent( datatype, &extent );
    num_segments = (original_count + count_by_segment - 1) / count_by_segment;
    segment_increment = (ptrdiff_t)count_by_segment * extent;

    sendtmpbuf = (char*) sendbuf; 
    if( sendbuf == MPI_IN_PLACE ) { 
        sendtmpbuf = (char *)recvbuf; 
    }

    OPAL_OUTPUT((ompi_coll_tuned_stream, "coll:tuned:reduce_generic count %d, msg size %ld, segsize %ld, max_requests %d",
                 original_count, (unsigned long)((ptrdiff_t)num_segments * (ptrdiff_t)segment_increment),
                 (unsigned long)segment_increment, max_outstanding_reqs));

    rank = ompi_comm_rank(comm);

    /* non-leaf nodes - wait for children to send me data & forward up 
       (if needed) */
    if( tree->tree_nextsize > 0 ) {
        ptrdiff_t real_segment_size;

        /* handle non existant recv buffer (i.e. its NULL) and 
           protect the recv buffer on non-root nodes */
        accumbuf = (char*)recvbuf;
        if( (NULL == accumbuf) || (root != rank) ) {
            /* Allocate temporary accumulator buffer. */
            size = opal_datatype_span(&datatype->super, original_count, &gap);
            accumbuf_free = (char*)malloc(size);
            if (accumbuf_free == NULL) { 
                line = __LINE__; ret = -1; goto error_hndl; 
            }
            accumbuf = accumbuf_free - gap;
        } 

        /* If this is a non-commutative operation we must copy
           sendbuf to the accumbuf, in order to simplfy the loops */
        if (!ompi_op_is_commute(op)) {
            ompi_datatype_copy_content_same_ddt(datatype, original_count, 
                                                (char*)accumbuf,
                                                (char*)sendtmpbuf);
        }
        /* Allocate two buffers for incoming segments */
        real_segment_size = opal_datatype_span(&datatype->super, count_by_segment, &gap);
        inbuf_free[0] = (char*) malloc(real_segment_size);
        if( inbuf_free[0] == NULL ) { 
            line = __LINE__; ret = -1; goto error_hndl; 
        }
        inbuf[0] = inbuf_free[0] - gap;
        /* if there is chance to overlap communication -
           allocate second buffer */
        if( (num_segments > 1) || (tree->tree_nextsize > 1) ) {
            inbuf_free[1] = (char*) malloc(real_segment_size);
            if( inbuf_free[1] == NULL ) { 
                line = __LINE__; ret = -1; goto error_hndl;
            }
            inbuf[1] = inbuf_free[1] - gap;
        } 

        /* reset input buffer index and receive count */
        inbi = 0;
        recvcount = 0;
        /* for each segment */
        for( segindex = 0; segindex <= num_segments; segindex++ ) {
            prevcount = recvcount;
            /* recvcount - number of elements in current segment */
            recvcount = count_by_segment;
            if( segindex == (num_segments-1) )
                recvcount = original_count - (ptrdiff_t)count_by_segment * (ptrdiff_t)segindex;

            /* for each child */
            for( i = 0; i < tree->tree_nextsize; i++ ) {
                /**
                 * We try to overlap communication:
                 * either with next segment or with the next child
                 */
                /* post irecv for current segindex on current child */
                if( segindex < num_segments ) {
                    void* local_recvbuf = inbuf[inbi];
                    if( 0 == i ) {
                        /* for the first step (1st child per segment) and 
                         * commutative operations we might be able to irecv 
                         * directly into the accumulate buffer so that we can 
                         * reduce(op) this with our sendbuf in one step as 
                         * ompi_op_reduce only has two buffer pointers, 
                         * this avoids an extra memory copy.
                         *
                         * BUT if the operation is non-commutative or 
                         * we are root and are USING MPI_IN_PLACE this is wrong!
                         */
                        if( (ompi_op_is_commute(op)) &&
                            !((MPI_IN_PLACE == sendbuf) && (rank == tree->tree_root)) ) {
                            local_recvbuf = accumbuf + (ptrdiff_t)segindex * (ptrdiff_t)segment_increment;
                        }
                    }

                    ret = MCA_PML_CALL(irecv(local_recvbuf, recvcount, datatype,
                                             tree->tree_next[i], 
                                             MCA_COLL_BASE_TAG_REDUCE, comm, 
                                             &reqs[inbi]));
                    if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl;}
                }
                /* wait for previous req to complete, if any.
                   if there are no requests reqs[inbi ^1] will be 
                   MPI_REQUEST_NULL. */
                /* wait on data from last child for previous segment */
                ret = ompi_request_wait_all( 1, &reqs[inbi ^ 1], 
                                             MPI_STATUSES_IGNORE );
                if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl;  }
                local_op_buffer = inbuf[inbi ^ 1];
                if( i > 0 ) {
                    /* our first operation is to combine our own [sendbuf] data 
                     * with the data we recvd from down stream (but only 
                     * the operation is commutative and if we are not root and 
                     * not using MPI_IN_PLACE)
                     */
                    if( 1 == i ) {
                        if( (ompi_op_is_commute(op)) && 
                            !((MPI_IN_PLACE == sendbuf) && (rank == tree->tree_root)) ) {
                            local_op_buffer = sendtmpbuf + (ptrdiff_t)segindex * (ptrdiff_t)segment_increment;
                        }
                    }
                    /* apply operation */
                    ompi_op_reduce(op, local_op_buffer, 
                                   accumbuf + (ptrdiff_t)segindex * (ptrdiff_t)segment_increment, 
                                   recvcount, datatype );
                } else if ( segindex > 0 ) {
                    void* accumulator = accumbuf + (ptrdiff_t)(segindex-1) * (ptrdiff_t)segment_increment;
                    if( tree->tree_nextsize <= 1 ) {
                        if( (ompi_op_is_commute(op)) &&
                            !((MPI_IN_PLACE == sendbuf) && (rank == tree->tree_root)) ) {
                            local_op_buffer = sendtmpbuf + (ptrdiff_t)(segindex-1) * (ptrdiff_t)segment_increment;
                        }
                    }
                    ompi_op_reduce(op, local_op_buffer, accumulator, prevcount, 
                                   datatype );

                    /* all reduced on available data this step (i) complete, 
                     * pass to the next process unless you are the root.
                     */
                    if (rank != tree->tree_root) {
                        /* send combined/accumulated data to parent */
                        ret = MCA_PML_CALL( send( accumulator, prevcount, 
                                                  datatype, tree->tree_prev, 
                                                  MCA_COLL_BASE_TAG_REDUCE,
                                                  MCA_PML_BASE_SEND_STANDARD, 
                                                  comm) );
                        if (ret != MPI_SUCCESS) { 
                            line = __LINE__; goto error_hndl;  
                        }
                    }

                    /* we stop when segindex = number of segments 
                       (i.e. we do num_segment+1 steps for pipelining */
                    if (segindex == num_segments) break;
                }

                /* update input buffer index */
                inbi = inbi ^ 1;
            } /* end of for each child */
        } /* end of for each segment */

        /* clean up */
        if( inbuf_free[0] != NULL) free(inbuf_free[0]);
        if( inbuf_free[1] != NULL) free(inbuf_free[1]);
        if( accumbuf_free != NULL ) free(accumbuf_free);
    }

    /* leaf nodes 
       Depending on the value of max_outstanding_reqs and 
       the number of segments we have two options:
       - send all segments using blocking send to the parent, or
       - avoid overflooding the parent nodes by limiting the number of 
       outstanding requests to max_oustanding_reqs.
       TODO/POSSIBLE IMPROVEMENT: If there is a way to determine the eager size 
       for the current communication, synchronization should be used only 
       when the message/segment size is smaller than the eager size.
    */
    else {

        /* If the number of segments is less than a maximum number of oustanding
           requests or there is no limit on the maximum number of outstanding 
           requests, we send data to the parent using blocking send */
        if ((0 == max_outstanding_reqs) || 
            (num_segments <= max_outstanding_reqs)) {
            
            segindex = 0;
            while ( original_count > 0) {
                if (original_count < count_by_segment) {
                    count_by_segment = original_count;
                }
                ret = MCA_PML_CALL( send((char*)sendbuf + 
                                         (ptrdiff_t)segindex * (ptrdiff_t)segment_increment,
                                         count_by_segment, datatype,
                                         tree->tree_prev, 
                                         MCA_COLL_BASE_TAG_REDUCE,
                                         MCA_PML_BASE_SEND_STANDARD,
                                         comm) );
                if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl; }
                segindex++;
                original_count -= count_by_segment;
            }
        }

        /* Otherwise, introduce flow control:
           - post max_outstanding_reqs non-blocking synchronous send,
           - for remaining segments
           - wait for a ssend to complete, and post the next one.
           - wait for all outstanding sends to complete.
        */
        else {

            int creq = 0;
            ompi_request_t **sreq = NULL;

            sreq = (ompi_request_t**) calloc( max_outstanding_reqs,
                                              sizeof(ompi_request_t*) );
            if (NULL == sreq) { line = __LINE__; ret = -1; goto error_hndl; }

            /* post first group of requests */
            for (segindex = 0; segindex < max_outstanding_reqs; segindex++) {
                ret = MCA_PML_CALL( isend((char*)sendbuf +
                                          (ptrdiff_t)segindex * (ptrdiff_t)segment_increment,
                                          count_by_segment, datatype,
                                          tree->tree_prev, 
                                          MCA_COLL_BASE_TAG_REDUCE,
                                          MCA_PML_BASE_SEND_SYNCHRONOUS, comm,
                                          &sreq[segindex]) );
                if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl;  }
                original_count -= count_by_segment;
            }

            creq = 0;
            while ( original_count > 0 ) {
                /* wait on a posted request to complete */
                ret = ompi_request_wait(&sreq[creq], MPI_STATUS_IGNORE);
                if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl;  }
                sreq[creq] = MPI_REQUEST_NULL;

                if( original_count < count_by_segment ) {
                    count_by_segment = original_count;
                }
                ret = MCA_PML_CALL( isend((char*)sendbuf + 
                                          (ptrdiff_t)segindex * (ptrdiff_t)segment_increment, 
                                          count_by_segment, datatype, 
                                          tree->tree_prev, 
                                          MCA_COLL_BASE_TAG_REDUCE, 
                                          MCA_PML_BASE_SEND_SYNCHRONOUS, comm, 
                                          &sreq[creq]) );
                if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl;  }
                creq = (creq + 1) % max_outstanding_reqs;
                segindex++;
                original_count -= count_by_segment;
            }

            /* Wait on the remaining request to complete */
            ret = ompi_request_wait_all( max_outstanding_reqs, sreq, 
                                         MPI_STATUSES_IGNORE );
            if (ret != MPI_SUCCESS) { line = __LINE__; goto error_hndl;  }

            /* free requests */
            free(sreq);
        }
    }
    return OMPI_SUCCESS;

 error_hndl:  /* error handler */
    opal_output_verbose(COLL_TUNED_VERBOSITY, ompi_coll_tuned_stream,
                        "ERROR_HNDL: node %d file %s line %d error %d\n", 
                        rank, __FILE__, line, ret );
    if( inbuf_free[0] != NULL ) free(inbuf_free[0]);
    if( inbuf_free[1] != NULL ) free(inbuf_free[1]);
    if( accumbuf_free != NULL ) free(accumbuf);
    return ret;
}

/* Attention: this version of the reduce operations does not
   work for:
   - non-commutative operations
   - segment sizes which are not multiplies of the extent of the datatype
     meaning that at least one datatype must fit in the segment !
*/

int ompi_coll_tuned_reduce_intra_chain( void *sendbuf, void *recvbuf, int count,
                                        ompi_datatype_t* datatype, 
                                        ompi_op_t* op, int root, 
                                        ompi_communicator_t* comm,
                                        mca_coll_base_module_t *module,
                                        uint32_t segsize, int fanout,
                                        int max_outstanding_reqs )
{
    int segcount = count;
    size_t typelng;
    mca_coll_tuned_module_t *tuned_module = (mca_coll_tuned_module_t*) module;
    mca_coll_tuned_comm_t *data = tuned_module->tuned_data;

    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_chain rank %d fo %d ss %5d", ompi_comm_rank(comm), fanout, segsize));

    COLL_TUNED_UPDATE_CHAIN( comm, tuned_module, root, fanout );
    /**
     * Determine number of segments and number of elements
     * sent per operation
     */
    ompi_datatype_type_size( datatype, &typelng );
    COLL_TUNED_COMPUTED_SEGCOUNT( segsize, typelng, segcount );

    return ompi_coll_tuned_reduce_generic( sendbuf, recvbuf, count, datatype, 
                                           op, root, comm, module,
                                           data->cached_chain, 
                                           segcount, max_outstanding_reqs );
}


int ompi_coll_tuned_reduce_intra_pipeline( void *sendbuf, void *recvbuf,
                                           int count, ompi_datatype_t* datatype,
                                           ompi_op_t* op, int root,
                                           ompi_communicator_t* comm,
                                           mca_coll_base_module_t *module,
                                           uint32_t segsize,
                                           int max_outstanding_reqs  )
{
    int segcount = count;
    size_t typelng;
    mca_coll_tuned_module_t *tuned_module = (mca_coll_tuned_module_t*) module;
    mca_coll_tuned_comm_t *data = tuned_module->tuned_data;

    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_pipeline rank %d ss %5d",
                 ompi_comm_rank(comm), segsize));

    COLL_TUNED_UPDATE_PIPELINE( comm, tuned_module, root );

    /**
     * Determine number of segments and number of elements
     * sent per operation
     */
    ompi_datatype_type_size( datatype, &typelng );
    COLL_TUNED_COMPUTED_SEGCOUNT( segsize, typelng, segcount );

    return ompi_coll_tuned_reduce_generic( sendbuf, recvbuf, count, datatype, 
                                           op, root, comm, module,
                                           data->cached_pipeline, 
                                           segcount, max_outstanding_reqs );
}

int ompi_coll_tuned_reduce_intra_binary( void *sendbuf, void *recvbuf,
                                         int count, ompi_datatype_t* datatype,
                                         ompi_op_t* op, int root,
                                         ompi_communicator_t* comm, 
                                         mca_coll_base_module_t *module,
                                         uint32_t segsize, 
                                         int max_outstanding_reqs  )
{
    int segcount = count;
    size_t typelng;
    mca_coll_tuned_module_t *tuned_module = (mca_coll_tuned_module_t*) module;
    mca_coll_tuned_comm_t *data = tuned_module->tuned_data;

    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_binary rank %d ss %5d",
                 ompi_comm_rank(comm), segsize));

    COLL_TUNED_UPDATE_BINTREE( comm, tuned_module, root );

    /**
     * Determine number of segments and number of elements
     * sent per operation
     */
    ompi_datatype_type_size( datatype, &typelng );
    COLL_TUNED_COMPUTED_SEGCOUNT( segsize, typelng, segcount );

    return ompi_coll_tuned_reduce_generic( sendbuf, recvbuf, count, datatype, 
                                           op, root, comm, module,
                                           data->cached_bintree, 
                                           segcount, max_outstanding_reqs );
}

int ompi_coll_tuned_reduce_intra_binomial( void *sendbuf, void *recvbuf,
                                           int count, ompi_datatype_t* datatype,
                                           ompi_op_t* op, int root,
                                           ompi_communicator_t* comm, 
                                           mca_coll_base_module_t *module,
                                           uint32_t segsize,
                                           int max_outstanding_reqs  )
{
    int segcount = count;
    size_t typelng;
    mca_coll_tuned_module_t *tuned_module = (mca_coll_tuned_module_t*) module;
    mca_coll_tuned_comm_t *data = tuned_module->tuned_data;


    printf("debugging, checking if the make succeed\n");

    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_binomial rank %d ss %5d",
                 ompi_comm_rank(comm), segsize));

    COLL_TUNED_UPDATE_IN_ORDER_BMTREE( comm, tuned_module, root );

    /**
     * Determine number of segments and number of elements
     * sent per operation
     */
    ompi_datatype_type_size( datatype, &typelng );
    COLL_TUNED_COMPUTED_SEGCOUNT( segsize, typelng, segcount );

    return ompi_coll_tuned_reduce_generic( sendbuf, recvbuf, count, datatype, 
                                           op, root, comm, module,
                                           data->cached_in_order_bmtree, 
                                           segcount, max_outstanding_reqs );
}

/*
 * reduce_intra_in_order_binary 
 * 
 * Function:      Logarithmic reduce operation for non-commutative operations.
 * Acecpts:       same as MPI_Reduce()
 * Returns:       MPI_SUCCESS or error code
 */
int ompi_coll_tuned_reduce_intra_in_order_binary( void *sendbuf, void *recvbuf,
                                                  int count, 
                                                  ompi_datatype_t* datatype,
                                                  ompi_op_t* op, int root,
                                                  ompi_communicator_t* comm, 
                                                  mca_coll_base_module_t *module,
                                                  uint32_t segsize,
                                                  int max_outstanding_reqs  )
{
    int ret, rank, size, io_root, segcount = count;
    void *use_this_sendbuf = NULL, *use_this_recvbuf = NULL;
    char *tmpbuf_free = NULL;
    size_t typelng;
    mca_coll_tuned_module_t *tuned_module = (mca_coll_tuned_module_t*) module;
    mca_coll_tuned_comm_t *data = tuned_module->tuned_data;

    rank = ompi_comm_rank(comm);
    size = ompi_comm_size(comm);
    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_in_order_binary rank %d ss %5d",
                 rank, segsize));

    COLL_TUNED_UPDATE_IN_ORDER_BINTREE( comm, tuned_module );

    /**
     * Determine number of segments and number of elements
     * sent per operation
     */
    ompi_datatype_type_size( datatype, &typelng );
    COLL_TUNED_COMPUTED_SEGCOUNT( segsize, typelng, segcount );

    /* An in-order binary tree must use root (size-1) to preserve the order of
       operations.  Thus, if root is not rank (size - 1), then we must handle
       1. MPI_IN_PLACE option on real root, and 
       2. we must allocate temporary recvbuf on rank (size - 1).
       Note that generic function must be careful not to switch order of 
       operations for non-commutative ops.
    */
    io_root = size - 1;
    use_this_sendbuf = sendbuf;
    use_this_recvbuf = recvbuf;
    if (io_root != root) {
        ptrdiff_t dsize, gap;
        char *tmpbuf;
    
        dsize = opal_datatype_span(&datatype->super, count, &gap);

        if ((root == rank) && (MPI_IN_PLACE == sendbuf)) {
            tmpbuf_free = (char *) malloc(dsize);
            if (NULL == tmpbuf_free) {
                return MPI_ERR_INTERN;
            }
            tmpbuf = tmpbuf_free - gap;
            ompi_datatype_copy_content_same_ddt(datatype, count, 
                                                (char*)tmpbuf,
                                                (char*)recvbuf);
            use_this_sendbuf = tmpbuf;
        } else if (io_root == rank) {
            tmpbuf_free = (char *) malloc(dsize);
            if (NULL == tmpbuf_free) {
                return MPI_ERR_INTERN;
            }
            tmpbuf = tmpbuf_free - gap;
            use_this_recvbuf = tmpbuf;
        }
    }

    /* Use generic reduce with in-order binary tree topology and io_root */
    ret = ompi_coll_tuned_reduce_generic( use_this_sendbuf, use_this_recvbuf, count, datatype,
                                          op, io_root, comm, module, 
                                          data->cached_in_order_bintree, 
                                          segcount, max_outstanding_reqs );
    if (MPI_SUCCESS != ret) { return ret; }

    /* Clean up */
    if (io_root != root) {
        if (root == rank) {
            /* Receive result from rank io_root to recvbuf */
            ret = MCA_PML_CALL(recv(recvbuf, count, datatype, io_root,
                                    MCA_COLL_BASE_TAG_REDUCE, comm,
                                    MPI_STATUS_IGNORE));
            if (MPI_SUCCESS != ret) { return ret; }
          
        } else if (io_root == rank) {
            /* Send result from use_this_recvbuf to root */
            ret = MCA_PML_CALL(send(use_this_recvbuf, count, datatype, root,
                                    MCA_COLL_BASE_TAG_REDUCE, 
                                    MCA_PML_BASE_SEND_STANDARD, comm));
            if (MPI_SUCCESS != ret) { return ret; }
        }
    }
    if (NULL != tmpbuf_free) {
        free(tmpbuf_free);
    }

    return MPI_SUCCESS;
}

/*
 * Linear functions are copied from the BASIC coll module
 * they do not segment the message and are simple implementations
 * but for some small number of nodes and/or small data sizes they 
 * are just as fast as tuned/tree based segmenting operations 
 * and as such may be selected by the decision functions
 * These are copied into this module due to the way we select modules
 * in V1. i.e. in V2 we will handle this differently and so will not
 * have to duplicate code.
 * GEF Oct05 after asking Jeff.
 */

/*
 *  reduce_lin_intra
 *
 *  Function:   - reduction using O(N) algorithm
 *  Accepts:    - same as MPI_Reduce()
 *  Returns:    - MPI_SUCCESS or error code
 */
int
ompi_coll_tuned_reduce_intra_basic_linear(void *sbuf, void *rbuf, int count,
                                          struct ompi_datatype_t *dtype,
                                          struct ompi_op_t *op,
                                          int root,
                                          struct ompi_communicator_t *comm,
                                          mca_coll_base_module_t *module)
{
    int i, rank, err, size;
    ptrdiff_t extent, dsize, gap;
    char *free_buffer = NULL, *pml_buffer = NULL;
    char *inplace_temp_free = NULL, *inbuf;

    /* Initialize */

    rank = ompi_comm_rank(comm);
    size = ompi_comm_size(comm);

    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_basic_linear rank %d", rank));

    /* If not root, send data to the root. */

    if (rank != root) {
        err = MCA_PML_CALL(send(sbuf, count, dtype, root,
                                MCA_COLL_BASE_TAG_REDUCE,
                                MCA_PML_BASE_SEND_STANDARD, comm));
        return err;
    }

    /* see discussion in ompi_coll_basic_reduce_lin_intra about 
       extent and true extent */
    /* for reducing buffer allocation lengths.... */

    dsize = opal_datatype_span(&dtype->super, count, &gap);
    ompi_datatype_type_extent(dtype, &extent);

    if (MPI_IN_PLACE == sbuf) {
        sbuf = rbuf;
        inplace_temp_free = (char*)malloc(dsize);
        if (NULL == inplace_temp_free) {
            return OMPI_ERR_OUT_OF_RESOURCE;
        }
        rbuf = inplace_temp_free - gap;
    }

    if (size > 1) {
        free_buffer = (char*)malloc(dsize);
        if (NULL == free_buffer) {
            err = OMPI_ERR_OUT_OF_RESOURCE;
            goto exit;
        }
        pml_buffer = free_buffer - gap;
    }

    /* Initialize the receive buffer. */

    if (rank == (size - 1)) {
        err = ompi_datatype_copy_content_same_ddt(dtype, count, (char*)rbuf, 
                                                  (char*)sbuf);
    } else {
        err = MCA_PML_CALL(recv(rbuf, count, dtype, size - 1,
                                MCA_COLL_BASE_TAG_REDUCE, comm,
                                MPI_STATUS_IGNORE));
    }
    if (MPI_SUCCESS != err) {
        goto exit;
    }

    /* Loop receiving and calling reduction function (C or Fortran). */

    for (i = size - 2; i >= 0; --i) {
        if (rank == i) {
            inbuf = (char*)sbuf;
        } else {
            err = MCA_PML_CALL(recv(pml_buffer, count, dtype, i,
                                    MCA_COLL_BASE_TAG_REDUCE, comm,
                                    MPI_STATUS_IGNORE));
            if (MPI_SUCCESS != err) {
                goto exit;
            }

            inbuf = pml_buffer;
        }

        /* Perform the reduction */
        ompi_op_reduce(op, inbuf, rbuf, count, dtype);
    }

    if (NULL != inplace_temp_free) {
        err = ompi_datatype_copy_content_same_ddt(dtype, count, (char*)sbuf, rbuf);
    }
    err = MPI_SUCCESS;

  exit:
    if (NULL != inplace_temp_free) {
        free(inplace_temp_free);
    }
    if (NULL != free_buffer) {
        free(free_buffer);
    }

    /* All done */
    return err;
}

/* copied function (with appropriate renaming) ends here */




/*addded Rabenseifner from the newest open mpi implementation wuyou*/
int
ompi_coll_tuned_reduce_intra_rabenseifner(void *sbuf, void *rbuf, int count,
                                          struct ompi_datatype_t *dtype,
                                          struct ompi_op_t *op,
                                          int root,
                                          struct ompi_communicator_t *comm,
                                          mca_coll_base_module_t *module){
    int comm_size = ompi_comm_size(comm);
    int rank = ompi_comm_rank(comm);

    OPAL_OUTPUT((ompi_coll_tuned_stream,
                 "coll:base:reduce_intra_redscat_gather: rank %d/%d, root %d",
                 rank, comm_size, root));

    /* Find nearest power-of-two less than or equal to comm_size */
    int nsteps = opal_hibit(comm_size, comm->c_cube_dim + 1);   /* ilog2(comm_size) */
    if (-1 == nsteps) {
        return MPI_ERR_ARG;
    }
    int nprocs_pof2 = 1 << nsteps;                              /* flp2(comm_size) */

    if (nprocs_pof2 < 2 || count < nprocs_pof2 || !ompi_op_is_commute(op)) {
        OPAL_OUTPUT((ompi_coll_tuned_stream,
                     "coll:base:reduce_intra_redscat_gather: rank %d/%d count %d "
                     "switching to basic linear reduce", rank, comm_size, count));
        return ompi_coll_base_reduce_intra_basic_linear(sbuf, rbuf, count, dtype,
                                                        op, root, comm, module);
    }

    int err = MPI_SUCCESS;
    int *rindex = NULL, *rcount = NULL, *sindex = NULL, *scount = NULL;
    ptrdiff_t lb, extent, dsize, gap;
    ompi_datatype_get_extent(dtype, &lb, &extent);
    dsize = opal_datatype_span(&dtype->super, count, &gap);

    /* Temporary buffers */
    char *tmp_buf_raw = NULL, *rbuf_raw = NULL;
    tmp_buf_raw = malloc(dsize);
    if (NULL == tmp_buf_raw) {
        err = OMPI_ERR_OUT_OF_RESOURCE;
        goto cleanup_and_return;
    }
    char *tmp_buf = tmp_buf_raw - gap;

    if (rank != root) {
        rbuf_raw = malloc(dsize);
        if (NULL == rbuf_raw) {
            err = OMPI_ERR_OUT_OF_RESOURCE;
            goto cleanup_and_return;
        }
        rbuf = rbuf_raw - gap;
    }

    if ((rank != root) || (sbuf != MPI_IN_PLACE)) {
        err = ompi_datatype_copy_content_same_ddt(dtype, count, rbuf,
                                                  (char *)sbuf);
        if (MPI_SUCCESS != err) { goto cleanup_and_return; }
    }

    /*
     * Step 1. Reduce the number of processes to the nearest lower power of two
     * p' = 2^{\floor{\log_2 p}} by removing r = p - p' processes.
     * 1. In the first 2r processes (ranks 0 to 2r - 1), all the even ranks send
     *    the second half of the input vector to their right neighbor (rank + 1)
     *    and all the odd ranks send the first half of the input vector to their
     *    left neighbor (rank - 1).
     * 2. All 2r processes compute the reduction on their half.
     * 3. The odd ranks then send the result to their left neighbors
     *    (the even ranks).
     *
     * The even ranks (0 to 2r - 1) now contain the reduction with the input
     * vector on their right neighbors (the odd ranks). The first r even
     * processes and the p - 2r last processes are renumbered from
     * 0 to 2^{\floor{\log_2 p}} - 1. These odd ranks do not participate in the
     * rest of the algorithm.
     */

    int vrank, step, wsize;
    int nprocs_rem = comm_size - nprocs_pof2;

    if (rank < 2 * nprocs_rem) {
        int count_lhalf = count / 2;
        int count_rhalf = count - count_lhalf;

        if (rank % 2 != 0) {
            /*
             * Odd process -- exchange with rank - 1
             * Send the left half of the input vector to the left neighbor,
             * Recv the right half of the input vector from the left neighbor
             */
            err = ompi_coll_base_sendrecv(rbuf, count_lhalf, dtype, rank - 1,
                                          MCA_COLL_BASE_TAG_REDUCE,
                                          (char *)tmp_buf + (ptrdiff_t)count_lhalf * extent,
                                          count_rhalf, dtype, rank - 1,
                                          MCA_COLL_BASE_TAG_REDUCE, comm,
                                          MPI_STATUS_IGNORE, rank);
            if (MPI_SUCCESS != err) { goto cleanup_and_return; }

            /* Reduce on the right half of the buffers (result in rbuf) */
            ompi_op_reduce(op, (char *)tmp_buf + (ptrdiff_t)count_lhalf * extent,
                           (char *)rbuf + count_lhalf * extent, count_rhalf, dtype);

            /* Send the right half to the left neighbor */
            err = MCA_PML_CALL(send((char *)rbuf + (ptrdiff_t)count_lhalf * extent,
                                    count_rhalf, dtype, rank - 1,
                                    MCA_COLL_BASE_TAG_REDUCE,
                                    MCA_PML_BASE_SEND_STANDARD, comm));
            if (MPI_SUCCESS != err) { goto cleanup_and_return; }

            /* This process does not pariticipate in recursive doubling phase */
            vrank = -1;

        } else {
            /*
             * Even process -- exchange with rank + 1
             * Send the right half of the input vector to the right neighbor,
             * Recv the left half of the input vector from the right neighbor
             */
            err = ompi_coll_base_sendrecv((char *)rbuf + (ptrdiff_t)count_lhalf * extent,
                                          count_rhalf, dtype, rank + 1,
                                          MCA_COLL_BASE_TAG_REDUCE,
                                          tmp_buf, count_lhalf, dtype, rank + 1,
                                          MCA_COLL_BASE_TAG_REDUCE, comm,
                                          MPI_STATUS_IGNORE, rank);
            if (MPI_SUCCESS != err) { goto cleanup_and_return; }

            /* Reduce on the right half of the buffers (result in rbuf) */
            ompi_op_reduce(op, tmp_buf, rbuf, count_lhalf, dtype);

            /* Recv the right half from the right neighbor */
            err = MCA_PML_CALL(recv((char *)rbuf + (ptrdiff_t)count_lhalf * extent,
                                    count_rhalf, dtype, rank + 1,
                                    MCA_COLL_BASE_TAG_REDUCE, comm,
                                    MPI_STATUS_IGNORE));
            if (MPI_SUCCESS != err) { goto cleanup_and_return; }

            vrank = rank / 2;
        }
    } else { /* rank >= 2 * nprocs_rem */
        vrank = rank - nprocs_rem;
    }

    /*
     * Step 2. Reduce-scatter implemented with recursive vector halving and
     * recursive distance doubling. We have p' = 2^{\floor{\log_2 p}}
     * power-of-two number of processes with new ranks (vrank) and result in rbuf.
     *
     * The even-ranked processes send the right half of their buffer to rank + 1
     * and the odd-ranked processes send the left half of their buffer to
     * rank - 1. All processes then compute the reduction between the local
     * buffer and the received buffer. In the next \log_2(p') - 1 steps, the
     * buffers are recursively halved, and the distance is doubled. At the end,
     * each of the p' processes has 1 / p' of the total reduction result.
     */

    rindex = malloc(sizeof(*rindex) * nsteps);    /* O(\log_2(p)) */
    sindex = malloc(sizeof(*sindex) * nsteps);
    rcount = malloc(sizeof(*rcount) * nsteps);
    scount = malloc(sizeof(*scount) * nsteps);
    if (NULL == rindex || NULL == sindex || NULL == rcount || NULL == scount) {
        err = OMPI_ERR_OUT_OF_RESOURCE;
        goto cleanup_and_return;
    }

    if (vrank != -1) {
        step = 0;
        wsize = count;
        sindex[0] = rindex[0] = 0;

        for (int mask = 1; mask < nprocs_pof2; mask <<= 1) {
            /*
             * On each iteration: rindex[step] = sindex[step] -- beginning of the
             * current window. Length of the current window is storded in wsize.
             */
            int vdest = vrank ^ mask;
            /* Translate vdest virtual rank to real rank */
            int dest = (vdest < nprocs_rem) ? vdest * 2 : vdest + nprocs_rem;

            if (rank < dest) {
                /*
                 * Recv into the left half of the current window, send the right
                 * half of the window to the peer (perform reduce on the left
                 * half of the current window)
                 */
                rcount[step] = wsize / 2;
                scount[step] = wsize - rcount[step];
                sindex[step] = rindex[step] + rcount[step];
            } else {
                /*
                 * Recv into the right half of the current window, send the left
                 * half of the window to the peer (perform reduce on the right
                 * half of the current window)
                 */
                scount[step] = wsize / 2;
                rcount[step] = wsize - scount[step];
                rindex[step] = sindex[step] + scount[step];
            }

            /* Send part of data from the rbuf, recv into the tmp_buf */
            err = ompi_coll_base_sendrecv((char *)rbuf + (ptrdiff_t)sindex[step] * extent,
                                          scount[step], dtype, dest,
                                          MCA_COLL_BASE_TAG_REDUCE,
                                          (char *)tmp_buf + (ptrdiff_t)rindex[step] * extent,
                                          rcount[step], dtype, dest,
                                          MCA_COLL_BASE_TAG_REDUCE, comm,
                                          MPI_STATUS_IGNORE, rank);
            if (MPI_SUCCESS != err) { goto cleanup_and_return; }

            /* Local reduce: rbuf[] = tmp_buf[] <op> rbuf[] */
            ompi_op_reduce(op, (char *)tmp_buf + (ptrdiff_t)rindex[step] * extent,
                           (char *)rbuf + (ptrdiff_t)rindex[step] * extent,
                           rcount[step], dtype);

            /* Move the current window to the received message */
            if (step + 1 < nsteps) {
                rindex[step + 1] = rindex[step];
                sindex[step + 1] = rindex[step];
                wsize = rcount[step];
                step++;
            }
        }
    }
    /*
     * Assertion: each process has 1 / p' of the total reduction result:
     * rcount[nsteps - 1] elements in the rbuf[rindex[nsteps - 1], ...].
     */

    /*
     * Setup the root process for gather operation.
     * Case 1: root < 2r and root is odd -- root process was excluded on step 1
     *         Recv data from process 0, vroot = 0, vrank = 0
     * Case 2: root < 2r and root is even: vroot = root / 2
     * Case 3: root >= 2r: vroot = root - r
     */
    int vroot = 0;
    if (root < 2 * nprocs_rem) {
        if (root % 2 != 0) {
            vroot = 0;
            if (rank == root) {
                /*
                 * Case 1: root < 2r and root is odd -- root process was
                 * excluded on step 1 (newrank == -1).
                 * Recv a data from the process 0.
                 */
                rindex[0] = 0;
                step = 0, wsize = count;
                for (int mask = 1; mask < nprocs_pof2; mask *= 2) {
                    rcount[step] = wsize / 2;
                    scount[step] = wsize - rcount[step];
                    rindex[step] = 0;
                    sindex[step] = rcount[step];
                    step++;
                    wsize /= 2;
                }

                err = MCA_PML_CALL(recv(rbuf, rcount[nsteps - 1], dtype, 0,
                                        MCA_COLL_BASE_TAG_REDUCE, comm,
                                        MPI_STATUS_IGNORE));
                if (MPI_SUCCESS != err) { goto cleanup_and_return; }
                vrank = 0;

            } else if (vrank == 0) {
                /* Send a data to the root */
                err = MCA_PML_CALL(send(rbuf, rcount[nsteps - 1], dtype, root,
                                        MCA_COLL_BASE_TAG_REDUCE,
                                        MCA_PML_BASE_SEND_STANDARD, comm));
                if (MPI_SUCCESS != err) { goto cleanup_and_return; }
                vrank = -1;
            }
        } else {
            /* Case 2: root < 2r and a root is even: vroot = root / 2 */
            vroot = root / 2;
        }
    } else {
        /* Case 3: root >= 2r: newroot = root - r */
        vroot = root - nprocs_rem;
    }

    /*
     * Step 3. Gather result at the vroot by the binomial tree algorithm.
     * Each process has 1 / p' of the total reduction result:
     * rcount[nsteps - 1] elements in the rbuf[rindex[nsteps - 1], ...].
     * All exchanges are executed in reverse order relative
     * to recursive doubling (previous step).
     */

    if (vrank != -1) {
        int vdest_tree, vroot_tree;
        step = nsteps - 1; /* step = ilog2(p') - 1 */

        for (int mask = nprocs_pof2 >> 1; mask > 0; mask >>= 1) {
            int vdest = vrank ^ mask;
            /* Translate vdest virtual rank to real rank */
            int dest = (vdest < nprocs_rem) ? vdest * 2 : vdest + nprocs_rem;
            if ((vdest == 0) && (root < 2 * nprocs_rem) && (root % 2 != 0))
                dest = root;

            vdest_tree = vdest >> step;
            vdest_tree <<= step;
            vroot_tree = vroot >> step;
            vroot_tree <<= step;
            if (vdest_tree == vroot_tree) {
                /* Send data from rbuf and exit */
                err = MCA_PML_CALL(send((char *)rbuf + (ptrdiff_t)rindex[step] * extent,
                                        rcount[step], dtype, dest,
                                        MCA_COLL_BASE_TAG_REDUCE,
                                        MCA_PML_BASE_SEND_STANDARD, comm));
                if (MPI_SUCCESS != err) { goto cleanup_and_return; }
                break;
            } else {
                /* Recv and continue */
                err = MCA_PML_CALL(recv((char *)rbuf + (ptrdiff_t)sindex[step] * extent,
                                        scount[step], dtype, dest,
                                        MCA_COLL_BASE_TAG_REDUCE, comm,
                                        MPI_STATUS_IGNORE));
                if (MPI_SUCCESS != err) { goto cleanup_and_return; }
            }
            step--;
        }
    }

  cleanup_and_return:
    if (NULL != tmp_buf_raw)
        free(tmp_buf_raw);
    if (NULL != rbuf_raw)
        free(rbuf_raw);
    if (NULL != rindex)
        free(rindex);
    if (NULL != sindex)
        free(sindex);
    if (NULL != rcount)
        free(rcount);
    if (NULL != scount)
        free(scount);
    return err;

}

/*rabenseifner copied open mpi end*/




/**
 * The following are used by dynamic and forced rules
 *
 * publish details of each algorithm and if its forced/fixed/locked in
 * as you add methods/algorithms you must update this and the query/map routines
 *
 * this routine is called by the component only
 * this makes sure that the mca parameters are set to their initial values and 
 * perms module does not call this they call the forced_getvalues routine 
 * instead.
 */

int ompi_coll_tuned_reduce_intra_check_forced_init (coll_tuned_force_algorithm_mca_param_indices_t *mca_param_indices)
{
    mca_base_var_enum_t*new_enum;

    ompi_coll_tuned_forced_max_algorithms[REDUCE] = coll_tuned_reduce_algorithm_count;

    (void) mca_base_component_var_register(&mca_coll_tuned_component.super.collm_version,
                                           "reduce_algorithm_count",
                                           "Number of reduce algorithms available",
                                           MCA_BASE_VAR_TYPE_INT, NULL, 0,
                                           MCA_BASE_VAR_FLAG_DEFAULT_ONLY,
                                           OPAL_INFO_LVL_5,
                                           MCA_BASE_VAR_SCOPE_CONSTANT,
                                           &coll_tuned_reduce_algorithm_count);

    /* MPI_T: This variable should eventually be bound to a communicator */
    coll_tuned_reduce_forced_algorithm = 0;
    (void) mca_base_var_enum_create("coll_tuned_reduce_algorithms", reduce_algorithms, &new_enum);
    mca_param_indices->algorithm_param_index =
        mca_base_component_var_register(&mca_coll_tuned_component.super.collm_version,
                                        "reduce_algorithm",
                                        "Which reduce algorithm is used. Can be locked down to choice of: 0 ignore, 1 linear, 2 chain, 3 pipeline, 4 binary, 5 binomial, 6 in-order binary",
                                        MCA_BASE_VAR_TYPE_INT, new_enum, 0, 0,
                                        OPAL_INFO_LVL_5,
                                        MCA_BASE_VAR_SCOPE_READONLY,
                                        &coll_tuned_reduce_forced_algorithm);
    OBJ_RELEASE(new_enum);
    if (mca_param_indices->algorithm_param_index < 0) {
        return mca_param_indices->algorithm_param_index;
    }

    coll_tuned_reduce_segment_size = 0;
    mca_param_indices->segsize_param_index =
        mca_base_component_var_register(&mca_coll_tuned_component.super.collm_version,
                                        "reduce_algorithm_segmentsize",
                                        "Segment size in bytes used by default for reduce algorithms. Only has meaning if algorithm is forced and supports segmenting. 0 bytes means no segmentation.",
                                        MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                        OPAL_INFO_LVL_5,
                                        MCA_BASE_VAR_SCOPE_READONLY,
                                        &coll_tuned_reduce_segment_size);

    coll_tuned_reduce_tree_fanout = ompi_coll_tuned_init_tree_fanout; /* get system wide default */
    mca_param_indices->tree_fanout_param_index =
        mca_base_component_var_register(&mca_coll_tuned_component.super.collm_version,
                                        "reduce_algorithm_tree_fanout",
                                        "Fanout for n-tree used for reduce algorithms. Only has meaning if algorithm is forced and supports n-tree topo based operation.",
                                        MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                        OPAL_INFO_LVL_5,
                                        MCA_BASE_VAR_SCOPE_READONLY,
                                        &coll_tuned_reduce_tree_fanout);

    coll_tuned_reduce_chain_fanout = ompi_coll_tuned_init_chain_fanout; /* get system wide default */
    mca_param_indices->chain_fanout_param_index = 
      mca_base_component_var_register(&mca_coll_tuned_component.super.collm_version,
                                      "reduce_algorithm_chain_fanout",
                                      "Fanout for chains used for reduce algorithms. Only has meaning if algorithm is forced and supports chain topo based operation.",
                                      MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                      OPAL_INFO_LVL_5,
                                      MCA_BASE_VAR_SCOPE_READONLY,
                                      &coll_tuned_reduce_chain_fanout);

    coll_tuned_reduce_max_requests = 0; /* no limit for reduce by default */
    mca_param_indices->max_requests_param_index = 
      mca_base_component_var_register(&mca_coll_tuned_component.super.collm_version,
                                      "reduce_algorithm_max_requests",
                                      "Maximum number of outstanding send requests on leaf nodes. 0 means no limit.",
                                      MCA_BASE_VAR_TYPE_INT, NULL, 0, 0,
                                      OPAL_INFO_LVL_5,
                                      MCA_BASE_VAR_SCOPE_READONLY,
                                      &coll_tuned_reduce_max_requests);
    if (mca_param_indices->max_requests_param_index < 0) {
        return mca_param_indices->max_requests_param_index;
    }

    if (coll_tuned_reduce_max_requests < 0) {
        if( 0 == ompi_comm_rank( MPI_COMM_WORLD ) ) {
            opal_output( 0, "Maximum outstanding requests must be positive number or 0.  Initializing to 0 (no limit).\n" );
        }
        coll_tuned_reduce_max_requests = 0;
    }

    return (MPI_SUCCESS);
}


int ompi_coll_tuned_reduce_intra_do_forced(void *sbuf, void* rbuf, int count,
                                           struct ompi_datatype_t *dtype,
                                           struct ompi_op_t *op, int root,
                                           struct ompi_communicator_t *comm,
                                           mca_coll_base_module_t *module)
{
    mca_coll_tuned_module_t *tuned_module = (mca_coll_tuned_module_t*) module;
    mca_coll_tuned_comm_t *data = tuned_module->tuned_data;

    const int segsize      = data->user_forced[REDUCE].segsize;
    const int chain_fanout = data->user_forced[REDUCE].chain_fanout;
    const int max_requests = data->user_forced[REDUCE].max_requests;

    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_do_forced selected algorithm %d", 
                 data->user_forced[REDUCE].algorithm));


    switch (data->user_forced[REDUCE].algorithm) {
    case (0):  return ompi_coll_tuned_reduce_intra_dec_fixed (sbuf, rbuf, count, dtype, 
                                                              op, root, comm, module);
    case (1):  return ompi_coll_tuned_reduce_intra_basic_linear (sbuf, rbuf, count, dtype,
                                                                 op, root, comm, module);
    case (2):  return ompi_coll_tuned_reduce_intra_chain (sbuf, rbuf, count, dtype,
                                                          op, root, comm, module,
                                                          segsize, chain_fanout, max_requests);
    case (3):  return ompi_coll_tuned_reduce_intra_pipeline (sbuf, rbuf, count, dtype,
                                                             op, root, comm, module,
                                                             segsize, max_requests);
    case (4):  return ompi_coll_tuned_reduce_intra_binary (sbuf, rbuf, count, dtype,
                                                           op, root, comm, module,
                                                           segsize, max_requests);
    case (5):  return ompi_coll_tuned_reduce_intra_binomial (sbuf, rbuf, count, dtype,
                                                             op, root, comm, module,
                                                             segsize, max_requests);
    case (6):  return ompi_coll_tuned_reduce_intra_in_order_binary(sbuf, rbuf, count, dtype,
                                                                   op, root, comm, module,
                                                                   segsize, max_requests);
    case (7):  return ompi_coll_tuned_reduce_intra_rabenseifner(sbuf, rbuf, count, dtype,
                                                                   op, root, comm, module,
                                                                   segsize, max_requests);
    default:
        OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_do_forced attempt to select algorithm %d when only 0-%d is valid?",
                     data->user_forced[REDUCE].algorithm, ompi_coll_tuned_forced_max_algorithms[REDUCE]));
        return (MPI_ERR_ARG);
    } /* switch */
}


int ompi_coll_tuned_reduce_intra_do_this(void *sbuf, void* rbuf, int count,
                                         struct ompi_datatype_t *dtype,
                                         struct ompi_op_t *op, int root,
                                         struct ompi_communicator_t *comm,
                                         mca_coll_base_module_t *module,
                                         int algorithm, int faninout, 
                                         int segsize, int max_requests )
{
    OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_do_this selected algorithm %d topo faninout %d segsize %d",
                 algorithm, faninout, segsize));

    switch (algorithm) {
    case (0):  return ompi_coll_tuned_reduce_intra_dec_fixed (sbuf, rbuf, count, dtype,
                                                              op, root, comm, module);
    case (1):  return ompi_coll_tuned_reduce_intra_basic_linear (sbuf, rbuf, count, dtype, 
                                                                 op, root, comm, module);
    case (2):  return ompi_coll_tuned_reduce_intra_chain (sbuf, rbuf, count, dtype,
                                                          op, root, comm, module,
                                                          segsize, faninout, max_requests);
    case (3):  return ompi_coll_tuned_reduce_intra_pipeline (sbuf, rbuf, count, dtype,
                                                             op, root, comm, module,
                                                             segsize, max_requests);
    case (4):  return ompi_coll_tuned_reduce_intra_binary (sbuf, rbuf, count, dtype,
                                                           op, root, comm, module,
                                                           segsize, max_requests); 
    case (5):  return ompi_coll_tuned_reduce_intra_binomial (sbuf, rbuf, count, dtype,
                                                             op, root, comm, module,
                                                             segsize, max_requests); 
    case (6):  return ompi_coll_tuned_reduce_intra_in_order_binary(sbuf, rbuf, count, dtype,
                                                                   op, root, comm, module,
                                                                   segsize, max_requests);
    case (7):  return ompi_coll_tuned_reduce_intra_rabenseifner(sbuf, rbuf, count, dtype,
                                                                   op, root, comm, module,
                                                                   segsize, max_requests);


    default:
        OPAL_OUTPUT((ompi_coll_tuned_stream,"coll:tuned:reduce_intra_do_this attempt to select algorithm %d when only 0-%d is valid?",
                     algorithm, ompi_coll_tuned_forced_max_algorithms[REDUCE]));
        return (MPI_ERR_ARG);
    } /* switch */
}

