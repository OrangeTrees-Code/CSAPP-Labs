/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) ((unsigned)((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4
#define DSIZE 8

#define CHUNKSIZE (1 << 12)

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))
#define PACK_ALL(size, prev_alloc, alloc) ((size) | (prev_alloc) | (alloc))

#define GET(p) (*((unsigned *)p))
#define PUT(p, val) ((*((unsigned *)p)) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) (GET(p) & 0x2)
#define SET_ALLOC(p) (GET(p) |= 0x1)
#define SET_PREV_ALLOC(p) (GET(p) |= 0x2)
#define SET_FREE(p) (GET(p) &= (~0x1))
#define SET_PREV_FREE(p) (GET(p) &= (~0x2))

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))

static char *heap_listp = 0;

#define FREE_LIST_NUM 15

static char **free_lists;

#define PREV_NODE(bp) ((char *)(mem_heap_lo() + (*(unsigned *)(bp))))
#define NEXT_NODE(bp) ((char *)(mem_heap_lo() + *(unsigned *)((char *)(bp) + WSIZE)))
#define SET_PREV_NODE(bp, val) (*((unsigned *)(bp))) = ((unsigned)(long)(val))
#define SET_NEXT_NODE(bp, val) (*((unsigned *)((char *)bp + WSIZE))) = ((unsigned)(long)(val))

#define CHECK_ALIGN(p) (ALIGN(p) == (size_t)(p))

static inline void get_range(size_t index);
static size_t low_range;
static size_t high_range;

static inline void *extend_heap(size_t words);
static inline void *coalesce(void *bp, size_t size);
static inline size_t get_index(size_t size);
static inline size_t adjust_alloc_size(size_t size);
static inline void *find_fit(size_t asize);
static inline void place(void *bp, size_t size);
static inline void insert_node(void *bp, size_t size);
static inline void delete_node(void *bp);

#include <stdarg.h>
#include <stdio.h>

void dbg_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    free_lists = mem_heap_lo();

    for (int i = 0; i < FREE_LIST_NUM; i++)
    {
        if ((heap_listp = mem_sbrk(DSIZE)) == (void *)(-1))
        {
            return -1;
        }

        free_lists[i] = mem_heap_lo();
    }

    if ((heap_listp = mem_sbrk(2 * DSIZE)) == (void *)-1)
    {
        return -1;
    }

    PUT(heap_listp, 0);

    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));

    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));

    PUT(heap_listp + (3 * WSIZE), PACK(0, 3));

    heap_listp += DSIZE;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
    {
        return -1;
    }

    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
        return NULL;
    else
    {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    };
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    free(ptr);
    return newptr;
}

static inline size_t get_index(size_t size)
{
    if (size <= 24)
        return 0;
    if (size <= 32)
        return 1;
    if (size <= 64)
        return 2;
    if (size <= 80)
        return 3;
    if (size <= 120)
        return 4;
    if (size <= 240)
        return 5;
    if (size <= 480)
        return 6;
    if (size <= 960)
        return 7;
    if (size <= 1920)
        return 8;
    if (size <= 3840)
        return 9;
    if (size <= 7680)
        return 10;
    if (size <= 15360)
        return 11;
    if (size <= 30720)
        return 12;
    if (size <= 61440)
        return 13;
    else
        return 14;
}

static inline void insert_node(void *bp, size_t size)
{
    size_t index = get_index(size);
    char *cur = free_lists[index];

    free_lists[index] = bp;
    if (cur == mem_heap_lo())
    {
        SET_NEXT_NODE(bp, NULL);
        SET_PREV_NODE(bp, NULL);
    }
    else
    {
        SET_NEXT_NODE(bp, cur);
        SET_PREV_NODE(bp, NULL);
        SET_PREV_NODE(cur, bp);
    }
}

static inline void delete_node(void *bp)
{
    char *prev = PREV_NODE(bp);
    char *next = NEXT_NODE(bp);

    if (prev == mem_heap_lo())
    {
        size_t size = GET_SIZE(HDRP(bp));
        size_t index = get_index(size);
        free_lists[index] = next;
        if (next != mem_heap_lo())
        {
            SET_PREV_NODE(next, NULL);
        }
    }
    else
    {
        SET_NEXT_NODE(prev, next);
        if (next != mem_heap_lo())
        {
            SET_PREV_NODE(next, prev);
        }
    }
}

static inline void *coalesce(void *bp, size_t size)
{
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_NODE(bp)));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    if (next_alloc && prev_alloc)
    {
        SET_PREV_FREE(HDRP(NEXT_BLKP(bp)));
    }
    else if (prev_alloc && !next_alloc)
    {
        delete_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK_ALL(size, 0x2, 0));

        PUT(FTRP(bp), PACK_ALL(size, 0x2, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        delete_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK_ALL(size, prev_prev_alloc, 0));
        PUT(FTRP(bp), PACK_ALL(size, prev_prev_alloc, 0));
        bp = PREV_BLKP(bp);
    }
    else
    {
        delete_node(PREV_BLKP(bp));
        delete_node(NEXT_BLKP(bp));

        size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp))));

        size_t prev_prev_alloc = GET_PREV_ALLOC(HDRP(PREV_BLKP(bp)));

        PUT(HDRP(PREV_BLKP(bp)), PACK_ALL(size, prev_prev_alloc, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK_ALL(size, prev_prev_alloc, 0));
        bp = PREV_BLKP(bp);
    }
    insert_node(bp, size);
    return bp;
}
static inline void *extend_heap(size_t words)
{
    size_t size = words % 2 == 0 ? words * WSIZE : (words + 1) * WSIZE;
    char *bp;
    if ((long)(bp = mem_sbrk(size)) == -1)
    {
        return NULL;
    }

    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    PUT(HDRP(bp), PACK_ALL(size, prev_alloc, 0));

    PUT(FTRP(bp), PACK_ALL(size, prev_alloc, 0));

    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp, size);
}
static inline size_t adjust_alloc_size(size_t size)
{
    if (size >= 120 && size < 128)
    {
        return 128;
    }
    if (size >= 448 && size < 512)
    {
        return 512;
    }
    if (size >= 1000 && size < 1024)
    {
        return 1024;
    }
    if (size >= 2000 && size < 2048)
    {
        return 2048;
    }
    return size;
}
static inline void *find_fit(size_t asize)
{

    for (size_t index = get_index(asize); index < FREE_LIST_NUM; index++)
    {
        for (char *bp = free_lists[index]; bp != mem_heap_lo(); bp = NEXT_NODE(bp))
        {
            if (GET_SIZE(HDRP(bp)) >= asize)
            {
                return bp;
            }
        }
    }

    return NULL;
}
static inline void place(void *bp, size_t size)
{
    size_t rest = GET_SIZE(HDRP(bp)) - size;

    delete_node(bp);

    if (rest < 2 * DSIZE)
    {
        SET_ALLOC(HDRP(bp));

        SET_PREV_ALLOC(HDRP(NEXT_BLKP(bp)));

        if (!GET_ALLOC(HDRP(NEXT_BLKP(bp))))
        {
            SET_PREV_ALLOC(FTRP(NEXT_BLKP(bp)));
        }
    }
    else
    {
        PUT(HDRP(bp), PACK_ALL(size, GET_PREV_ALLOC(HDRP(bp)), 1));

        PUT(HDRP(NEXT_BLKP(bp)), PACK_ALL(rest, 0x2, 0));

        PUT(FTRP(NEXT_BLKP(bp)), PACK_ALL(rest, 0x2, 0));

        insert_node(NEXT_BLKP(bp), rest);
    }
}
void *malloc(size_t size)
{
    if (heap_listp == 0)
    {
        mm_init();
    }

    if (size == 0)
    {
        return NULL;
    }

    size = adjust_alloc_size(size);
    size_t asize;
    if (size <= DSIZE)
    {
        asize = 2 * DSIZE;
    }
    else
    {
        asize = DSIZE * ((size + (WSIZE) + (DSIZE - 1)) / DSIZE);
    }
    char *bp;
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    size_t extend_size = asize > CHUNKSIZE ? asize : CHUNKSIZE;

    if ((bp = extend_heap(extend_size / WSIZE)) != NULL)
    {
        place(bp, asize);
        return bp;
    }
    else
    {
        return NULL;
    }
}
void free(void *bp)
{
    if (bp == NULL)
    {
        return;
    }
    if (heap_listp == 0)
    {
        mm_init();
        return;
    }
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    PUT(HDRP(bp), PACK_ALL(size, prev_alloc, 0));
    PUT(FTRP(bp), PACK_ALL(size, prev_alloc, 0));
    coalesce(bp, size);
}

void *realloc(void *ptr, size_t size)
{
    if (size == 0)
    {
        free(ptr);
        return 0;
    }

    if (ptr == NULL)
    {
        return malloc(size);
    }

    void *new;
    if ((new = malloc(size)) == NULL)
    {
        return NULL;
    }

    size_t oldSize = GET_SIZE(HDRP(ptr));
    oldSize = MIN(oldSize, size);
    memcpy(ptr, new, oldSize);

    return new;
}

void *calloc(size_t elem_num, size_t size)
{
    size_t total = size * elem_num;
    void *new = malloc(total);
    memset(new, 0, total);
    return new;
}

static inline void get_range(size_t index)
{
    switch (index)
    {
    case 0:
        low_range = 8;
        high_range = 24;
        break;
    case 1:
        low_range = 24;
        high_range = 32;
        break;
    case 2:
        low_range = 32;
        high_range = 64;
        break;
    case 3:
        low_range = 64;
        high_range = 80;
        break;
    case 4:
        low_range = 80;
        high_range = 120;
        break;
    case 5:
        low_range = 120;
        high_range = 240;
        break;
    case 6:
        low_range = 240;
        high_range = 480;
        break;
    case 7:
        low_range = 480;
        high_range = 960;
        break;
    case 8:
        low_range = 960;
        high_range = 1920;
        break;
    case 9:
        low_range = 1920;
        high_range = 3840;
        break;
    case 10:
        low_range = 3840;
        high_range = 7680;
        break;
    case 11:
        low_range = 7680;
        high_range = 15360;
        break;
    case 12:
        low_range = 15360;
        high_range = 30720;
        break;
    case 13:
        low_range = 30720;
        high_range = 61440;
        break;
    case 14:
        low_range = 61440;
        high_range = 0x7fffffff;
        break;
    }
}

/*
 * mm_checkheap: 检查堆的正确性
 */
void mm_checkheap(int lineno)
{
    // 偏移掉分配在堆底的空闲链表
    char *bp = mem_heap_lo() + DSIZE * FREE_LIST_NUM;
    // 检查序言块
    if (GET(bp) != 0)
    {
        dbg_printf("[%d]Prologue Error: word before prologue incorrect at %p\n", lineno, bp);
    }
    if (GET(bp + WSIZE) != PACK(DSIZE, 1))
    {
        dbg_printf("[%d]Prologue Error: prologue header incorrect at %p\n", lineno, bp);
    }
    if (GET(bp + DSIZE) != PACK(DSIZE, 1))
    {
        dbg_printf("[%d]Prologue Error: prologue footer incorrect at %p\n", lineno, bp);
    }
    // 移动指针到序言块之后
    bp += DSIZE;
    // 初始化为1而不是2，用以辨别初始状态（即指针指向堆底时）
    size_t is_prev_alloc = 1;
    size_t is_prev_free = 0;

    while ((void *)bp < mem_heap_hi())
    {
        // 检查边界是否对齐
        if (!CHECK_ALIGN(bp))
        {
            dbg_printf("[%d]Alignment Error: block not aligned at %p\n", lineno, bp);
        }
        // 检查块大小是否合法
        if (GET_SIZE(HDRP(bp)) == 0)
        {
            dbg_printf("[%d]Block Header Error: block size is invalid at %p\n", lineno, bp);
        }
        // 指针并非指向堆底时，检查头部是否正确标记上一个块是否分配
        if (is_prev_alloc != 1)
        {
            if (GET_PREV_ALLOC(HDRP(bp)) != is_prev_alloc)
            {
                dbg_printf("[%d]Block Header Error: prev alloc bit is incorrect at %p\n", lineno, bp);
            }
        }
        is_prev_alloc = GET_ALLOC(HDRP(bp));

        // 对于空闲块，检查头部尾部是否一致
        if (!GET_ALLOC(HDRP(bp)))
        {
            // 首先检查头尾是否一致
            if (GET(HDRP(bp)) != GET(FTRP(bp)))
            {
                dbg_printf("[%d]Block Match Error: header does not match footer at %p\n", lineno, bp);
            }
            // 检查是否存在连续空闲块
            if (is_prev_free)
            {
                dbg_printf("[%d]Block Free Error: two consecutive free blocks at %p\n", lineno, bp);
            }
            is_prev_free = 1;
        }
        else
        {
            is_prev_free = 0;
        }
    }
    // 检查结尾块
    // 检查结尾块大小是否为0
    if (GET_SIZE(HDRP(bp)) != 0)
    {
        dbg_printf("[%d]Epilogue Error: epilogue block size is invalid at %p\n", lineno, bp);
    }
    // 检查结尾块是否正确标记上一个块是否分配
    if (GET_PREV_ALLOC(HDRP(bp)) != is_prev_alloc)
    {
        dbg_printf("[%d]Epilogue Error: prev alloc bit is incorrect at %p\n", lineno, bp);
    }
    // 检查结尾块是否正确标记当前块是否分配
    if (GET_ALLOC(HDRP(bp)) != 1)
    {
        dbg_printf("[%d]Epilogue Error: epilogue block is not allocated at %p\n", lineno, bp);
    }
    // 检查当前指针是否超过堆顶
    if ((void *)bp > mem_heap_hi())
    {
        dbg_printf("[%d]Heap Error: heap extends beyond heap top at %p\n", lineno, bp);
    }
    // 检查是否对齐堆顶
    if (!CHECK_ALIGN(bp))
    {
        dbg_printf("[%d]Alignment Error: heap top not aligned at %p\n", lineno, bp);
    }
}

/*
 * mm_checkfreelist: 检查空闲链表的正确性
 */
void mm_checkfreelist(int lineno)
{
    // 初始化对比变量
    size_t free_block_by_list = 0;
    size_t free_block_by_heap = 0;
    // 检查所有链表链接的正确性
    for (int i = 0; i < FREE_LIST_NUM; i++)
    {
        char *bp = free_lists[i];
        // 设置全局变量 low_range high_range 对应当前桶的大小范围
        get_range(i);
        while ((void *)bp > mem_heap_lo() && (void *)bp < mem_heap_hi())
        {
            // 检查双向链表是否匹配
            if (PREV_NODE(bp) != NULL)
            {
                if (NEXT_NODE(PREV_NODE(bp)) != bp)
                {
                    dbg_printf("[%d]Free List Error: prev and next pointer not match at %p\n", lineno, bp);
                }
            }
            // 检查当前节点大小是否符合桶大小
            size_t cur_size = GET_SIZE(HDRP(bp));
            if (cur_size < low_range || cur_size > high_range)
            {
                dbg_printf("[%d]Free List Error: block size not match bucket at %p\n", lineno, bp);
            }
            // 检查当前节点是否为空闲块
            if (GET_ALLOC(HDRP(bp)))
            {
                dbg_printf("[%d]Free List Error: block is not free at %p\n", lineno, bp);
            }
            bp = NEXT_NODE(bp);
            ++free_block_by_list;
        }
        if ((void *)bp != mem_heap_lo())
        {
            dbg_printf("[%d]Free List Error: pointer out of range at %p\n", lineno, bp);
        }
    }
    char *bp = mem_heap_lo() + DSIZE * FREE_LIST_NUM;
    while ((void *)bp < mem_heap_hi())
    {
        if (!GET_ALLOC(HDRP(bp)))
        {
            ++free_block_by_heap;
        }
        bp = NEXT_BLKP(bp);
    }
    // 检查对比变量是否匹配，从而确定是否所有的空闲块都在空闲链表中
    if (free_block_by_heap != free_block_by_list)
    {
        dbg_printf("[%d]Free List Error: not all free block in free lists %p\n", lineno, bp);
    }
}
