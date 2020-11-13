#include <stdio.h>

struct extent_t {
    int ofst;
    int len;
};
typedef struct extent_t extent_t;

int data_index[] = {
    0, 1, 2, 3, -1, 7 ,8 ,-1 ,9 , 10 ,-1 , -1 ,
    511 , 512 , 513 ,514 ,
};

static void merge(extent_t *e , int *n) {
    int i , j ;
    extent_t *p = e - 1;
    int in_found_ctx = 0;
    for (i = 0 , j = 0; i < 16; ++i) {
        int ba = data_index[i];
        if(ba == -1) {
            in_found_ctx = 0;
            continue;
        } else if (ba != -1 && in_found_ctx) {
            if (p->len + p->ofst == ba
                && (p->ofst >> 9 == ba >> 9)) {
                p->len++;
            } else {
                ++p;
                ++j;
                p->ofst = data_index[i];
                p->len = 1;
            }  
        } else if (ba != -1 && !in_found_ctx) {
            ++j;
            ++p;
            p->ofst = data_index[i];
            p->len = 1;
            in_found_ctx = 1;
        }
    }
    *n = j;
}

static void extent_to_bitmap_id( int *bid , int *n , const extent_t *e , const int ne ) {
    int i;
    *n = 0;
    for (i = 0 ; i < ne ; ++i) {
        int j;
        int in = 0;
        for ( j = 0 ; j < *n ; ++j) {
            if( (e[i].ofst >> 9) == bid[j]) {
                in = 1;
                break;
            }
        }
        if(!in) {
            bid[*n++] = (e[i].ofst>>9);
        }
    }
} 



int main() {
    extent_t e[64] = {0};
    int ne;
    int bid[64] = {0};
    int i;
    merge(e, &ne);
    
    printf("ne=%d , exts=",ne);
    for(i = 0 ; i < ne ; ++i) {
        printf("{%d~%d}" , e[i].ofst , e[i].len);
    }
    printf("\n");

    int nbid;
    extent_to_bitmap_id(bid, &nbid, e , ne);

    printf("nbid=%d , bids=",ne);
    for(i = 0 ; i < nbid ; ++i) {
        printf("{%d}", bid[i]);
    }
    printf("\n");
    return 0;
}