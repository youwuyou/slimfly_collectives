#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <random>
#include <algorithm>

int get_dest(int rank, int iteration, int size){
  if(iteration == 0){
    return -1;
  }
  return (rank + iteration) % size;
}

int get_source(int rank, int iteration, int size){
  if(iteration == 0){
    return -1;
  }
  return (rank - iteration) < 0 ? size + (rank - iteration): (rank - iteration);
}

int MY_Alltoall_Linear(const void* buffer_send,
		       int count_send,
		       MPI_Datatype datatype_send,
		       void* buffer_recv,
		       int count_recv,
		       MPI_Datatype datatype_recv,
		       MPI_Comm communicator){
  int size, rank;
  MPI_Comm_size(communicator, &size);
  MPI_Comm_rank(communicator, &rank);
  for(size_t i = 0; i < size; i++){
    int dest = get_dest(rank, i, size);
    int source = get_source(rank, i, size);
    MPI_Request reqs[2];
    if(dest != -1){
      MPI_Isend(&(((char*)buffer_send)[i*count_send]), count_send, datatype_send, dest, 0, communicator, &(reqs[0]));
    }
    if(source != -1){
      MPI_Irecv(&(((char*)buffer_recv)[i*count_recv]), count_recv, datatype_recv, source, 0, communicator, &(reqs[1]));
    }
    if(dest != -1 && source != -1){
      MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
    }else if(dest != -1){
      MPI_Wait(&(reqs[0]), MPI_STATUS_IGNORE);
    }else if(source != -1){
      MPI_Wait(&(reqs[1]), MPI_STATUS_IGNORE);
    }
  }
  return 0;
}

int myrandom (int i) { return rand()%i;}

int MY_Alltoall_Random(const void* buffer_send,
                       int count_send,
                       MPI_Datatype datatype_send,
                       void* buffer_recv,
                       int count_recv,
                       MPI_Datatype datatype_recv,
                       MPI_Comm communicator, std::vector<int>* send_to){
  int size, rank;
  MPI_Comm_size(communicator, &size);
  MPI_Comm_rank(communicator, &rank);

  for(size_t i = 1; i < size; i++){
    int dest = send_to[rank][i];
    int source = -1;
    for(size_t j = 0; j < size; j++){
      if(send_to[j][i] == rank){
	source = j;
	break;
      }
    }
    //printf("Rank %d at iteration %d sending to %d and receiveing from %d\n", rank, i, dest, source);
    MPI_Request reqs[2];
    MPI_Isend(&(((char*)buffer_send)[i*count_send]), count_send, datatype_send, dest, 0, communicator, &(reqs[0]));
    MPI_Irecv(&(((char*)buffer_recv)[i*count_recv]), count_recv, datatype_recv, source, 0, communicator, &(reqs[1]));
    MPI_Waitall(2, reqs, MPI_STATUSES_IGNORE);
  }
  return 0;
}

int main(int argc, char** argv){
  long warmup = 5;
  MPI_Init(&argc, &argv);
  int len = atoi(argv[1]);
  long iterations = atol(argv[2]);
  int size;
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  char* buf_s = (char*) malloc(sizeof(char)*len*size);
  char* buf_r = (char*) malloc(sizeof(char)*len*size);

  std::vector<int> *send_to = (std::vector<int>*) malloc(sizeof(std::vector<int>)*size);
  FILE *fp;
  char filename[128];
  snprintf(filename, sizeof(filename), "alltoall_schedule_%d.txt", size);
  fp = fopen(filename, "r");
  for(size_t i = 0; i < size; i++){
    for(size_t j = 0; j < size; j++){
      int read;
      fscanf(fp, "%d", &read);
      send_to[i].push_back(read);
    }
  }

  double start = 0;
  for(long i = -warmup; i < iterations; i++){
    if(i == 0){
      start = MPI_Wtime();
    }
    //MY_Alltoall_Linear(buf_s, len, MPI_CHAR, buf_r, len, MPI_CHAR, MPI_COMM_WORLD);
    MY_Alltoall_Random(buf_s, len, MPI_CHAR, buf_r, len, MPI_CHAR, MPI_COMM_WORLD, send_to);
  }
  double time_s = (MPI_Wtime () - start);
  printf("Time (us): %f\n", (time_s / iterations)*1000000.0);
  fclose(fp);
  MPI_Finalize();
  return 0;
}
