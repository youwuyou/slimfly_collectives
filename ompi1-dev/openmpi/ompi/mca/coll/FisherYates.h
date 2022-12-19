#if !defined(FSHRYTS)
#define FSHRYTS

void FisherYates(int * arr, int size){
    for(int i = size - 1; i > 0; --i){
        int j = rand() % i;
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

#endif // FSHRYTS
