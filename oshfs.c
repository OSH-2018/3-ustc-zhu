#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <stdio.h>

//#define debug

struct filenode                         
{
    unsigned int pieces;                         //how many pieces the file divided into
    unsigned int logic_addr;                     //the logic address of the file
    char filename[33];                  //the length of filename is 32 (except '\0')
    unsigned int content[8142];                //support 254.4375mb
    struct stat st;
    struct filenode *next;
};


struct headnode
{
    char mem_flag[8192];                                 //record whether the mem is used 
    struct filenode *next;     
    size_t block_left; 
};

static const size_t size = 256 * 1024 * (size_t)1024;      //256mb
static const size_t blocksize = 32 * (size_t)1024;        //size of block 32kb
static const size_t blocknr = 8192;                       //the number of blocks

void *mem[8192];                                    

static struct headnode *root = NULL;


static int my_malloc(void)
{
    #ifdef debug
    printf("this is my_malloc\n");
    #endif

    int i;

    if (root->block_left == 0)
    {
    	return -ENOSPC;                                                  //no available block 
    }

    for (i = 0; i < blocknr; ++i)
    {
    	if (root->mem_flag[i] == 0)
    	{
            mem[i] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            root->mem_flag[i] = 1;
            root->block_left--;

            #ifdef debug
            printf("block_left = %d\n", root->block_left);
            #endif

            break;                                               //find an available block,then break
    	}
    }
    return i;
}

static void my_free(int i)
{
    #ifdef debug
    printf("this is my_free\n");
    #endif 

    munmap(mem[i], blocksize);
    root->mem_flag[i] = 0;
    root->block_left++;
}

static int my_realloc(struct filenode* node,int new_pieces)
{
    #ifdef debug
    printf("this is my_realloc\n");
    #endif

    int pre_pieces = node->pieces;

    if (new_pieces < pre_pieces)
    {
    	for (int i = new_pieces; i < pre_pieces; ++i)
    	{
    		my_free(node->content[i]);
    		node->pieces--;
    	}
    	return 1;
    }

    else{
    	if((new_pieces - node->pieces) > root->block_left)
    		return -1;
   		else
    	{
    		for (int i = pre_pieces; i < new_pieces; ++i)
    		{
    			node->content[i] = my_malloc();
                node->pieces++;
    		}
    		return 1;
   		}
    }

}

static struct filenode *get_filenode(const char *name)          
{
    #ifdef debug
    printf("this is get_filenode\n");
    #endif

    struct filenode *node = root->next;
    while(node)
    {
        #ifdef debug
        printf("%p\n", node);
        #endif

        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;

    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)        
{
    #ifdef debug
    printf("this is creat_filenode\n");
    #endif

    int i = my_malloc();
    struct filenode *new = (struct filenode *)mem[i];
    memcpy(new->filename, filename, strlen(filename) + 1);
    memcpy(&(new->st), st, sizeof(struct stat));
    new->pieces = 0;

    new->logic_addr = i;

    new->next = root->next;
   
    root->next = new;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    #ifdef debug
    printf("this is oshfs_init\n");
    #endif

    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                                  //store the beginning address of the blocks

    root = (struct headnode*)mem[0];                     //init root
    root->next = NULL;
    root->mem_flag[0] = 1;
    root->block_left = 8192;

    for (int i = 1; i < 8192; ++i)
        root->mem_flag[i] = 0;
    

    /*for(int i = 1; i < blocknr; i++)
    {
        mem[i] = (char *)mem[0] + blocksize * i;
        memset(mem[i], 0, blocksize);
    }
    //每一个mem的起始地址的设置
    for(int i = 1; i < blocknr; i++)
        munmap(mem[i], blocksize);
    */
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    #ifdef debug
    printf("this is oshfs_getattr\n");
    #endif

    int ret = 0;
    struct filenode *node = get_filenode(path);         
    if(strcmp(path, "/") == 0)                          
    {
        memset(stbuf, 0, sizeof(struct stat));
        stbuf->st_mode = S_IFDIR | 0755;                
    }
    else if(node)                                       
        memcpy(stbuf, &(node->st), sizeof(struct stat));
    else                                                
        ret = -ENOENT;
    return ret;
}

static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    #ifdef debug
    printf("this is oshfs_readdir\n");
    #endif

    struct filenode *node = root->next;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    while(node)
    {                                       
        filler(buf, node->filename, &(node->st), 0);
        node = node->next;
    }
    return 0;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)           
{
    #ifdef debug
    printf("this is oshfs_mknod\n");
    #endif

    struct stat st;                                                         
    st.st_mode = S_IFREG | 0644;                                            
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;                                                        
    st.st_size = 0;                                                         
    create_filenode(path + 1, &st);                                         
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)          //open file
{
    #ifdef debug
    printf("this is oshfs_open\n");
    #endif

    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    #ifdef debug
    printf("this is oshfs_write\n");
    #endif

    unsigned int new_pieces, pre_size;
    int success;
    struct filenode *node = get_filenode(path);                     //open
    pre_size = node->st.st_size;
    if(offset + size > node->st.st_size)
        node->st.st_size = offset + size;                              //change the size of the file
    new_pieces = (node->st.st_size % blocksize) ? ((int)node->st.st_size / blocksize + 1) : ((int)node->st.st_size / blocksize);
    success = my_realloc(node, new_pieces);          
    node->pieces = new_pieces;

    #ifdef debug
    printf("block_left = %d\n", root->block_left);
    printf("in oshfs_write the new_pieces is = %d\n", new_pieces);
    #endif

    if (success != -1)
    {
        #ifdef debug
        printf("my_realloc succeed!!!!\n");
        #endif
//        printf("%s\n", buf);
        int buf_add = 0;
        //int offset_block = (int)offset/blocksize, block_num = (size % blocksize) ? ((int)size / blocksize + 1) : ((int)size / blocksize);
        int offset_block = (int)offset/blocksize;
        #ifdef debug
        printf("offset_block=%d \n", offset_block);
        #endif

        int new_offset = offset % blocksize;
        int i = 0;
        while(buf_add < size)
        {
        	int copy_size;
            if(i == 0)
            {
        	    copy_size = blocksize - new_offset;
        	    memcpy(mem[node->content[offset_block + i]] + new_offset, buf + buf_add, copy_size);
                
                #ifdef debug                  
                printf("%s\n", (char*)mem[node->content[offset_block + i]]);
        	    #endif

                buf_add += copy_size;
            }
            else
            {
            	if((size - buf_add) > blocksize)	copy_size = blocksize;
            	else	copy_size = size - buf_add;
             	memcpy(mem[node->content[offset_block + i]], buf + buf_add, copy_size);

                #ifdef debug                  
                printf("%s\n", (char*)mem[node->content[offset_block + i]]);
        	    #endif

                buf_add += copy_size;
            }
            i++;
        }
        return size;
    }
    else
    	return -ENOMEM;
}

static int oshfs_truncate(const char *path, off_t size)     
{
    #ifdef debug
    printf("this is oshfs_truncate\n");
    #endif

    int success;
    struct filenode *node = get_filenode(path);             
    node->st.st_size = size;                               
    int new_pieces = (node->st.st_size % blocksize) ? ((int)node->st.st_size / blocksize + 1) : ((int)node->st.st_size / blocksize);
    success = my_realloc(node, new_pieces);

    #ifdef debug
    printf("in oshfs_truncate the new_pieces is = %d\n", new_pieces);           
    #endif
    
    if(success != -1){
        node->pieces = new_pieces;
        return 0;
    }
    else
    	return -ENOKEY;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
//从一个已经打开的文件中读出数据
{
    #ifdef debug
    printf("this is oshfs_read\n");
    #endif

    struct filenode *node = get_filenode(path);                     
    int ret = size;                                                 
    if(offset + size > node->st.st_size)                           
        ret = node->st.st_size - offset;                           
    int buf_add = 0;
    int offset_block = (int)offset/blocksize;
    int new_offset = offset % blocksize;
 
    int i = 0;
    while(buf_add < ret)
    {
    	int copy_size;
        if(i == 0)
        {
     	    //copy_size = blocksize - new_offset;
     	    if((ret - buf_add) > blocksize)	copy_size = blocksize - new_offset;
          	else	copy_size = ret - buf_add;
     	    memcpy(buf + buf_add, mem[node->content[offset_block + i]] + new_offset, copy_size);           
            printf("%s\n", (char*)mem[node->content[offset_block + i]]);
      	    buf_add += copy_size;
        }
        else
        {
        	if((ret - buf_add) > blocksize)	copy_size = blocksize;
          	else	copy_size = ret - buf_add;
          	memcpy(buf + buf_add, mem[node->content[offset_block + i]], copy_size);                 
            printf("%s\n", (char*)mem[node->content[offset_block + i]]);
      	    buf_add += copy_size;
        }
        i++;
    }   
    return ret;                                                    
}

static int oshfs_unlink(const char *path)               
{
    #ifdef debug
    printf("this is oshfs_unlink\n");
    #endif

    struct filenode *node1 = get_filenode(path);
    struct filenode *node2 = root->next;
    if (node1==root->next)                     
    {
        root->next=node1->next;
        node1->next=NULL;
        for (int i = 0; i < node1->pieces; ++i)	my_free(node1->content[i]);
    	my_free(node1->logic_addr);
        
        #ifdef debug
        printf("after rm block_left = %d\n", root->block_left);
        #endif
    	return 0;    
    }
    else if (node1)                         
    {
        while(node2->next != node1 && node2 != NULL)
            node2 = node2->next;
        node2->next = node1->next;
        node1->next = NULL;
    	for (int i = 0; i < node1->pieces; ++i)	my_free(node1->content[i]);
    	my_free(node1->logic_addr);

        #ifdef debug
        printf("after rm block_left = %d\n", root->block_left);
        #endif

    	return 0;      
    }

    else return 0;
    

}

static const struct fuse_operations op = {              
    .init = oshfs_init,
    .getattr = oshfs_getattr,
    .readdir = oshfs_readdir,
    .mknod = oshfs_mknod,
    .open = oshfs_open,
    .write = oshfs_write,
    .truncate = oshfs_truncate,
    .read = oshfs_read,
    .unlink = oshfs_unlink,
};

int main(int argc, char *argv[])
{
    return fuse_main(argc, argv, &op, NULL);           
}
