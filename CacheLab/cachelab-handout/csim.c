#include "cachelab.h"
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>

void printUsage() {
    puts("Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t <file>");
    puts("Options:");
    puts("  -h         Print this help message.");
    puts("  -v         Optional verbose flag.");
    puts("  -s <num>   Number of set index bits.");
    puts("  -E <num>   Number of lines per set.");
    puts("  -b <num>   Number of block offset bits.");
    puts("  -t <file>  Trace file.");
    puts("");
    puts("Examples:");
    puts("  linux>  ./csim -s 4 -E 1 -b 4 -t traces/yi.trace");
    puts("  linux>  ./csim -v -s 8 -E 2 -b 4 -t traces/yi.trace");
}
struct line
{
    int valid;
    int tag;
    int last_used_time;
};

typedef struct line* set;

set* cache;

int v = 0,s,E,b,t,timeStamp = 0;

int hit = 0,miss = 0, eviction = 0;

void initCache(){
    cache = (set*)malloc(sizeof(set) * (1<<s));
    for(int i = 0;i<(1<<s);i++){
        cache[i] = (set)malloc(sizeof(struct line) * E);
        for(int j = 0;j<E;j++){
            cache[i][j].valid = -1;
            cache[i][j].tag = -1;
            cache[i][j].last_used_time = -1;
        }
    }
}

void useCache(size_t address,int is_modify){
    int set_pos = (address>>b) & ((1<<s)-1);
    int tag = address>>(b+s);

    set cur_set = cache[set_pos];
    int lru_pos = 0,lru_time = cur_set[0].last_used_time;

    for(int i = 0;i<E;i++){
        if(cur_set[i].tag == tag){
            hit++;
            hit+=is_modify;

            cur_set[i].last_used_time = timeStamp;

            if(v){
                printf("hit\n");
            }

            return;
        }

        if(cur_set[i].last_used_time<lru_time){
            lru_time = cur_set[i].last_used_time;
            lru_pos = i;
        }
    }

    miss++;

    hit+=is_modify;

    eviction += (lru_time!=-1);

    if (v) {
        if (lru_time != -1) {
            if (is_modify)
                printf("miss eviction hit\n");
            else
                printf("miss eviction\n");
        }
        else {
            printf("miss\n");
        }
    }

    cur_set[lru_pos].last_used_time = timeStamp;
    cur_set[lru_pos].tag = tag;
}

int main(int argc,char* argv[])
{
    FILE* trace_file;

    if(argc==1){
        printUsage();
        exit(0);
    }
    
    int option;
    while((option = getopt(argc,argv,"hvs:E:b:t:"))!=-1){
        switch(option){
        case 'h':
            printUsage();
            exit(0);
        case 'v':
            v = 1;
            break;
        case 's':
            s = atoi(optarg);
            break;
        case 'E':
            E = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 't':
            trace_file = fopen(optarg,"r");
            break;
        default:
            printUsage();
            exit(0);

        }
    }

    if (s <= 0 || E <= 0 || b <= 0 || s + b > 64 || trace_file == NULL) {
        printUsage();
        exit(1);
    }

    initCache();

    int size;
    char operation;
    size_t address;

    while(fscanf(trace_file,"%s %lx,%d\n",&operation,&address,&size) == 3){
        timeStamp++;

        if(v){
            printf("%c %lx,%d ",operation,address,size);
        }

        switch(operation){
        case 'I':
            continue;
        case 'M':
            useCache(address,1);
            break;
        case 'L':case 'S':
            useCache(address,0);
        }
    }

    free(cache);
    printSummary(hit,miss, eviction);
    return 0;
}
