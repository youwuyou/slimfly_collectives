/*
 * Copyright (c) 2006      The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2006      The Technical University of Chemnitz. All
 *                         rights reserved.
 * Copyright (c) 2013      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * Copyright (c) 2014-2016 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 *
 * Author(s): Torsten Hoefler <htor@cs.indiana.edu>
 *
 */

#include "opal/include/opal/align.h"
#include "ompi/op/op.h"

#include "nbc_internal.h"

static inline int red_sched_binomial (int rank, int p, int root, void *sendbuf, void *redbuf, char tmpredbuf, int count, MPI_Datatype datatype,
                                      MPI_Op op, char inplace, NBC_Schedule *schedule, NBC_Handle *handle);
static inline int red_sched_chain (int rank, int p, int root, void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype,
                                   MPI_Op op, int ext, size_t size, NBC_Schedule *schedule, NBC_Handle *handle, int fragsize);

static inline int red_sched_linear (int rank, int rsize, int root, void *sendbuf, void *recvbuf, void *tmpbuf, int count, MPI_Datatype datatype,
                                    MPI_Op op, NBC_Schedule *schedule, NBC_Handle *handle);

#ifdef NBC_CACHE_SCHEDULE
/* tree comparison function for schedule cache */
int NBC_Reduce_args_compare(NBC_Reduce_args *a, NBC_Reduce_args *b, void *param) {
  if( (a->sendbuf == b->sendbuf) &&
      (a->recvbuf == b->recvbuf) &&
      (a->count == b->count) && 
      (a->datatype == b->datatype) &&
      (a->op == b->op) &&
      (a->root == b->root) ) {
    return  0;
  }
  if( a->sendbuf < b->sendbuf ) {
    return -1;
  }
  return +1;
}
#endif

/* the non-blocking reduce */
int ompi_coll_libnbc_ireduce(void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype, 
                             MPI_Op op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request,
                             struct mca_coll_base_module_2_0_0_t *module) {
  int rank, p, res, segsize, size;
  MPI_Aint ext;
  NBC_Schedule *schedule;
  char *redbuf=NULL, inplace;
  char tmpredbuf = 0;
#ifdef NBC_CACHE_SCHEDULE
  NBC_Reduce_args *args, *found, search;
#endif
  enum { NBC_RED_BINOMIAL, NBC_RED_CHAIN } alg;
  NBC_Handle *handle;
  ompi_coll_libnbc_request_t **coll_req = (ompi_coll_libnbc_request_t**) request;
  ompi_coll_libnbc_module_t *libnbc_module = (ompi_coll_libnbc_module_t*) module;
  ptrdiff_t span, gap;

  NBC_IN_PLACE(sendbuf, recvbuf, inplace);
  
  rank = ompi_comm_rank (comm);
  p = ompi_comm_size (comm);

  res = NBC_Init_handle(comm, coll_req, libnbc_module);
  if(res != NBC_OK) { printf("Error in NBC_Init_handle(%i)\n", res); return res; }
  handle = (*coll_req);
  res = MPI_Type_extent(datatype, &ext);
  if (MPI_SUCCESS != res) { printf("MPI Error in MPI_Type_extent() (%i)\n", res); return res; }
  res = MPI_Type_size(datatype, &size);
  if (MPI_SUCCESS != res) { printf("MPI Error in MPI_Type_size() (%i)\n", res); return res; }
  
  /* only one node -> copy data */
  if((p == 1) && !inplace) {
    res = NBC_Copy(sendbuf, count, datatype, recvbuf, count, datatype, comm);
    if (NBC_OK != res) { printf("Error in NBC_Copy() (%i)\n", res); return res; }
  }

  span = opal_datatype_span(&datatype->super, count, &gap);

  /* algorithm selection */
  if (p > 4 || size * count < 65536 || !ompi_op_is_commute(op)) {
    alg = NBC_RED_BINOMIAL;
    if(rank == root) {
      /* root reduces in receivebuffer */
      handle->tmpbuf = malloc (span);
      redbuf = recvbuf;
    } else {
      /* recvbuf may not be valid on non-root nodes */
      ptrdiff_t span_align = OPAL_ALIGN(span, datatype->super.align, ptrdiff_t);
      handle->tmpbuf = malloc (span_align + span);
      redbuf = (char*)span_align - gap;
      tmpredbuf = 1;
    }
  } else {
    handle->tmpbuf = malloc (span);
    alg = NBC_RED_CHAIN;
    segsize = 16384/2;
  }
  if (NULL == handle->tmpbuf) { printf("Error in malloc() (%i)\n", res); return res; }

#ifdef NBC_CACHE_SCHEDULE
  /* search schedule in communicator specific tree */
  search.sendbuf=sendbuf;
  search.recvbuf=recvbuf;
  search.count=count;
  search.datatype=datatype;
  search.op=op;
  search.root=root;
  found = (NBC_Reduce_args*)hb_tree_search((hb_tree*)handle->comminfo->NBC_Dict[NBC_REDUCE], &search);
  if(found == NULL) {
#endif
    schedule = (NBC_Schedule*)malloc(sizeof(NBC_Schedule));
    if (NULL == schedule) { printf("Error in malloc() (%i)\n", res); return res; }

    res = NBC_Sched_create(schedule);
    if(res != NBC_OK) { printf("Error in NBC_Sched_create (%i)\n", res); return res; }

    switch(alg) {
      case NBC_RED_BINOMIAL:
        res = red_sched_binomial(rank, p, root, sendbuf, redbuf, tmpredbuf, count, datatype, op, inplace, schedule, handle);
        break;
      case NBC_RED_CHAIN:
        res = red_sched_chain(rank, p, root, sendbuf, recvbuf, count, datatype, op, ext, size, schedule, handle, segsize);
        break;
    }
    if (NBC_OK != res) { printf("Error in Schedule creation() (%i)\n", res); return res; }
    
    res = NBC_Sched_commit(schedule);
    if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_commit() (%i)\n", res); return res; }
#ifdef NBC_CACHE_SCHEDULE
    /* save schedule to tree */
    args = (NBC_Reduce_args*)malloc(sizeof(NBC_Alltoall_args));
    args->sendbuf=sendbuf;
    args->recvbuf=recvbuf;
    args->count=count;
    args->datatype=datatype;
    args->op=op;
    args->root=root;
    args->schedule=schedule;
	  res = hb_tree_insert ((hb_tree*)handle->comminfo->NBC_Dict[NBC_REDUCE], args, args, 0);
    if(res != 0) printf("error in dict_insert() (%i)\n", res);
    /* increase number of elements for Reduce */
    if(++handle->comminfo->NBC_Dict_size[NBC_REDUCE] > NBC_SCHED_DICT_UPPER) {
      NBC_SchedCache_dictwipe((hb_tree*)handle->comminfo->NBC_Dict[NBC_REDUCE], &handle->comminfo->NBC_Dict_size[NBC_REDUCE]);
    }
  } else {
    /* found schedule */
    schedule=found->schedule;
  }
#endif
  
  res = NBC_Start(handle, schedule);
  if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Start() (%i)\n", res); return res; }
  
  /* tmpbuf is freed with the handle */
  return NBC_OK;
}

int ompi_coll_libnbc_ireduce_inter(void* sendbuf, void* recvbuf, int count, MPI_Datatype datatype,
				   MPI_Op op, int root, struct ompi_communicator_t *comm, ompi_request_t ** request,
				   struct mca_coll_base_module_2_0_0_t *module) {
  int rank, res, rsize;
  NBC_Schedule *schedule;
  NBC_Handle *handle;
  ompi_coll_libnbc_request_t **coll_req = (ompi_coll_libnbc_request_t**) request;
  ompi_coll_libnbc_module_t *libnbc_module = (ompi_coll_libnbc_module_t*) module;
  ptrdiff_t span, gap;

  rank = ompi_comm_rank (comm);
  rsize = ompi_comm_remote_size (comm);

  res = NBC_Init_handle(comm, coll_req, libnbc_module);
  if(res != NBC_OK) { printf("Error in NBC_Init_handle(%i)\n", res); return res; }
  handle = (*coll_req);

  span = opal_datatype_span(&datatype->super, count, &gap);
  handle->tmpbuf = malloc (span);
  if (NULL == handle->tmpbuf) { printf("Error in malloc() (%i)\n", res); return res; }

  schedule = (NBC_Schedule*)malloc(sizeof(NBC_Schedule));
  if (NULL == schedule) { printf("Error in malloc() (%i)\n", res); return res; }

  res = NBC_Sched_create(schedule);
  if(res != NBC_OK) { printf("Error in NBC_Sched_create (%i)\n", res); return res; }

  res = red_sched_linear (rank, rsize, root, sendbuf, recvbuf, (void *)(-gap), count, datatype, op, schedule, handle);
  if (NBC_OK != res) { printf("Error in Schedule creation() (%i)\n", res); return res; }

  res = NBC_Sched_commit(schedule);
  if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_commit() (%i)\n", res); return res; }

  res = NBC_Start(handle, schedule);
  if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Start() (%i)\n", res); return res; }

  /* tmpbuf is freed with the handle */
  return NBC_OK;
}


/* binomial reduce
 * if op is not commutative, reduce on rank 0, and then send the result to root rank
 *
 * working principle:
 * - each node gets a virtual rank vrank
 * - the 'root' node get vrank 0 
 * - node 0 gets the vrank of the 'root'
 * - all other ranks stay identical (they do not matter)
 *
 * Algorithm:
 * pairwise exchange
 * round r: 
 *  grp = rank % 2^r
 *  if grp == 0: receive from rank + 2^(r-1) if it exists and reduce value
 *  if grp == 1: send to rank - 2^(r-1) and exit function
 *  
 * do this for R=log_2(p) rounds
 *    
 */
#define RANK2VRANK(rank, vrank, root) \
{ \
  vrank = rank; \
  if (rank == 0) vrank = root; \
  if (rank == root) vrank = 0; \
}
#define VRANK2RANK(rank, vrank, root) \
{ \
  rank = vrank; \
  if (vrank == 0) rank = root; \
  if (vrank == root) rank = 0; \
}
static inline int red_sched_binomial (int rank, int p, int root, void *sendbuf, void *redbuf, char tmpredbuf, int count, MPI_Datatype datatype,
                                      MPI_Op op, char inplace, NBC_Schedule *schedule, NBC_Handle *handle) {
  int firstred, vroot, vrank, vpeer, peer, res, maxr, r;
  char *rbuf, *lbuf, *buf, tmpbuf;
  int tmprbuf, tmplbuf;
  ptrdiff_t gap;
  (void)opal_datatype_span(&datatype->super, count, &gap);

  if (ompi_op_is_commute(op)) {
    vroot = root;
  } else {
    vroot = 0;
  }
  RANK2VRANK(rank, vrank, vroot);
  maxr = (int)ceil((log((double)p)/LOG2));

  if (rank != root) {
    inplace = 0;
  }

  /* ensure the result ends up in redbuf on vrank 0 */
  if (0 == (maxr%2)) {
    rbuf = (void *)(-gap);
    tmprbuf = true;
    lbuf = redbuf;
    tmplbuf = tmpredbuf;
  } else {
    lbuf = (void *)(-gap);
    tmplbuf = true;
    rbuf = redbuf;
    tmprbuf = tmpredbuf;
    if (inplace) {
        res = NBC_Copy(rbuf, count, datatype, ((char *)handle->tmpbuf)-gap, count, datatype, MPI_COMM_SELF);
        if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_recv() (%i)\n", res); return res; }
    }
  }

  firstred = 1;
  for (r = 1; r <= maxr ; ++r) {
    if ((vrank % (1 << r)) == 0) {
      /* we have to receive this round */
      vpeer = vrank + (1 << (r - 1));
      VRANK2RANK(peer, vpeer, vroot)
      if (peer < p) {
        /* we have to wait until we have the data */
        res = NBC_Sched_recv (rbuf, tmprbuf, count, datatype, peer, schedule);
        if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_recv() (%i)\n", res); return res; }
        /* we have to wait until we have the data */
        res = NBC_Sched_barrier(schedule);
        if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }
        /* perform the reduce in my local buffer */
        /* this cannot be done until handle->tmpbuf is unused :-( so barrier after the op */
        if (firstred && !inplace) {
          /* perform the reduce with the senbuf */
          res = NBC_Sched_op (sendbuf, false, rbuf, tmprbuf, count, datatype, op, schedule);
          firstred = 0;
        } else {
          /* perform the reduce in my local buffer */
          res = NBC_Sched_op (lbuf, tmplbuf, rbuf, tmprbuf, count, datatype, op, schedule);
        }
        if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_op() (%i)\n", res); return res; }

        res = NBC_Sched_barrier(schedule);
        if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }

        /* swap left and right buffers */
        buf = rbuf; rbuf = lbuf ; lbuf = buf;
        tmpbuf = tmprbuf; tmprbuf = tmplbuf; tmplbuf = tmpbuf;
      }
    } else {
      /* we have to send this round */
      vpeer = vrank - (1 << (r - 1));
      VRANK2RANK(peer, vpeer, vroot)
      if (firstred && !inplace) {
        /* we have to use the sendbuf in the first round .. */
        res = NBC_Sched_send (sendbuf, false, count, datatype, peer, schedule);
      } else {
        /* and the redbuf in all remaining rounds */
        res = NBC_Sched_send (lbuf, tmplbuf, count, datatype, peer, schedule);
      }
      if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_send() (%i)\n", res); return res; }
      /* leave the game */
      break;
    }
  }
  /* send to root if vroot ! root */
  if (vroot != root) {
    if (0 == rank) {
      res = NBC_Sched_send (redbuf, tmpredbuf, count, datatype, root, schedule);
      if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_send() (%i)\n", res); return res; }
    } else if (root == rank) {
      res = NBC_Sched_recv (redbuf, tmpredbuf, count, datatype, vroot, schedule);
      if (NBC_OK != res) { free(handle->tmpbuf); printf("Error in NBC_Sched_recv() (%i)\n", res); return res; }
    }
  }

  return NBC_OK;
}

/* chain send ... */ 
static inline int red_sched_chain(int rank, int p, int root, void *sendbuf, void *recvbuf, int count, MPI_Datatype datatype, MPI_Op op, int ext, size_t size, NBC_Schedule *schedule, NBC_Handle *handle, int fragsize) {
  int res, vrank, rpeer, speer, numfrag, fragnum, fragcount, thiscount;
  long offset;
  
  RANK2VRANK(rank, vrank, root);
  VRANK2RANK(rpeer, vrank+1, root);
  VRANK2RANK(speer, vrank-1, root);
  
  if(count == 0) return NBC_OK;
  
  numfrag = count*size/fragsize;
  if((count*size)%fragsize != 0) numfrag++;
  fragcount = count/numfrag;
  /*printf("numfrag: %i, count: %i, size: %i, fragcount: %i\n", numfrag, count, size, fragcount);*/

  for(fragnum = 0; fragnum < numfrag; fragnum++) {
    offset = fragnum*fragcount*ext;
    thiscount = fragcount;
    if(fragnum == numfrag-1) {
      /* last fragment may not be full */
      thiscount = count-fragcount*fragnum;
    }

    /* last node does not recv */
    if (vrank != p-1) {
      if (vrank == 0) {
        res = NBC_Sched_recv ((char *)recvbuf+offset, false, thiscount, datatype, rpeer, schedule);
      } else {
        res = NBC_Sched_recv ((char *) offset, true, thiscount, datatype, rpeer, schedule);
      }
      if (NBC_OK != res) { printf("Error in NBC_Sched_recv() (%i)\n", res); return res; }
      /* this barrier here seems awkward but isn't!!!! */
      res = NBC_Sched_barrier(schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }

      /* root reduces into receivebuf */
      if(vrank == 0) {
        res = NBC_Sched_op ((char *) sendbuf + offset, false, (char *) recvbuf + offset, false,
                             thiscount, datatype, op, schedule);
      } else {
        res = NBC_Sched_op ((char *) sendbuf + offset, false, (char *) offset, true, thiscount,
                             datatype, op, schedule);
      }
      if (NBC_OK != res) { printf("Error in NBC_Sched_op() (%i)\n", res); return res; }

      res = NBC_Sched_barrier(schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }
    }

    /* root does not send */
    if(vrank != 0) {
      /* rank p-1 has to send out of sendbuffer :) */
      if(vrank == p-1) {
        res = NBC_Sched_send((char*)sendbuf+offset, false, thiscount, datatype, speer, schedule);
      } else {
        res = NBC_Sched_send((char*)offset, true, thiscount, datatype, speer, schedule);
      }
      if (NBC_OK != res) { printf("Error in NBC_Sched_send() (%i)\n", res); return res; }
      /* this barrier here seems awkward but isn't!!!! */
      res = NBC_Sched_barrier(schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }
    }
  }

  return NBC_OK;
}

/* simple linear algorithm for intercommunicators */
static inline int red_sched_linear (int rank, int rsize, int root, void *sendbuf, void *recvbuf, void *tmpbuf, int count, MPI_Datatype datatype,
                                    MPI_Op op, NBC_Schedule *schedule, NBC_Handle *handle) {
  int res;
  char *rbuf, *lbuf, *buf;
  int tmprbuf, tmplbuf;

  if(count == 0) return NBC_OK;

  if (MPI_ROOT == root) {
    /* ensure the result ends up in recvbuf */
    if (0 == (rsize%2)) {
      lbuf = tmpbuf;
      tmplbuf = true;
      rbuf = recvbuf;
      tmprbuf = false;
    } else {
      rbuf = tmpbuf;
      tmprbuf = true;
      lbuf = recvbuf;
      tmplbuf = false;
    }

    res = NBC_Sched_recv (lbuf, tmplbuf, count, datatype, 0, schedule);
    if (NBC_OK != res) { printf("Error in NBC_Sched_recv() (%i)\n", res); return res; }

    for (int peer = 1 ; peer < rsize ; ++peer) {
      res = NBC_Sched_recv (rbuf, tmprbuf, count, datatype, peer, schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_recv() (%i)\n", res); return res; }

      res = NBC_Sched_barrier(schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }

      res = NBC_Sched_op (lbuf, tmplbuf, rbuf, tmprbuf, count, datatype, op, schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_op() (%i)\n", res); return res; }

      res = NBC_Sched_barrier(schedule);
      if (NBC_OK != res) { printf("Error in NBC_Sched_barrier() (%i)\n", res); return res; }

      /* swap left and right buffers */
      buf = rbuf; rbuf = lbuf ; lbuf = buf;
      tmprbuf ^= 1; tmplbuf ^= 1;
    }
  } else if (MPI_PROC_NULL != root) {
    res = NBC_Sched_send (sendbuf, false, count, datatype, root, schedule);
    if (NBC_OK != res) { printf("Error in NBC_Sched_send() (%i)\n", res); return res; }
  }

  return NBC_OK;
}
