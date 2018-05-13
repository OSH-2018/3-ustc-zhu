# 实验3

本次实验，我支持的内存大小是254.4375mb,约等于256mb。

### block定义

```
static const size_t size = 256 * 1024 * (size_t)1024;      //total size = 256mb
static const size_t blocksize = 32 * (size_t)1024;        //size of block 32kb
static const size_t blocknr = 8192;                       //the number of blocks
static size_t block_left = 8192;                          //how many blocks are left

static void *mem[8192];                                    //logic memory, size/blocksize
static int mem_flag[8192];                                 //record whether the mem is used   
```

我定义每一个block大小为32kb，总内存为256mb，那样总共有8192个block

### filenode定义

```
struct filenode                         
{
    unsigned int pieces;                         //how many pieces the file divided into
    unsigned int logic_addr;                     //the logic address of the file
    char filename[33];                           //the length of filename is 32 (except '\0')
    unsigned int content[8142];                  //support 254.4375mb
    struct stat st;
    struct filenode *next;
};
```

由content可见，最大的文件大小为254mb。对扩展文件大小我有以下几点想法

1. 用多级index
2. 扩大块的大小，是能存入更多的content

### 内存分配算法

```
static int my_malloc(void)
{
    int i;

    if (block_left == 0)
    {
    	return -ENOSPC;                                                  //no available block 
    }

    for (i = 0; i < blocknr; ++i)
    {
    	if (mem_flag[i] == 0)
    	{
            mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            mem_flag[i] = 1;
            block_left--;
            break;                                               //find an available block,then break
    	}
    }
    return i;
}
```

我采用了最为朴素的算法。就是从头开始遍历mem数组，找到第一个还未分配的mem，将其分配。

### 测试结果

![](.\result.png)

