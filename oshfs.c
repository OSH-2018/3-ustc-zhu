#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
#include <stdio.h>

struct filenode                         
{
    unsigned int pieces;                         //how many pieces the file divided into
    unsigned int logic_addr;                     //the logic address of the file
    char filename[33];                  //the length of filename is 32 (except '\0')
    unsigned int content[8142];                //support 254.4375mb
    struct stat st;
    struct filenode *next;
};


static const size_t size = 256 * 1024 * (size_t)1024;      //total size = 256mb
static const size_t blocksize = 32 * (size_t)1024;        //size of block 32kb
static const size_t blocknr = 8192;                       //the number of blocks
static size_t block_left = 8192;

static void *mem[8192];                                    //logic mem, size/blocksize
static int mem_flag[8192];                                 //record whether the mem is used   

static void *mem_begin;                                    //the beginning address of the blocks

static struct filenode *root = NULL;


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

static void my_free(int i)
{
    munmap(mem[i], blocksize);
    mem_flag[i] = 0;
    block_left++;
}

static int my_realloc(struct filenode* node,int new_pieces)
{
    if (new_pieces < node->pieces)
    {
    	for (int i = new_pieces; i < node->pieces; ++i)
    	{
    		my_free(node->content[i]);
    		node->pieces--;
    	}
    	return 1;
    }

    else{
    	if((new_pieces - node->pieces) > block_left)
    		return -1;
   		else
    	{
    		for (int i = node->pieces; i < new_pieces; ++i)
    		{
    			node->content[i] = my_malloc();
    		}
    		return 1;
   		}
    }

}

static struct filenode *get_filenode(const char *name)          
{
    struct filenode *node = root;
    while(node)
    {
        if(strcmp(node->filename, name + 1) != 0)
            node = node->next;
        else
            return node;

    }
    return NULL;
}

static void create_filenode(const char *filename, const struct stat *st)       
{
    int i = my_malloc();
    struct filenode *new = (struct filenode *)mem[i];
    memcpy(new->filename, filename, strlen(filename) + 1);
    memcpy(&(new->st), st, sizeof(struct stat));
    new->pieces = 0;

    new->logic_addr = i;

    new->next = root;
   
    root = new;
}

static void *oshfs_init(struct fuse_conn_info *conn)
{
    mem[0] = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    mem_begin = mem[0];                                //store the beginning address of the blocks
    for(int i = 0; i < blocknr; i++)
    {
        mem[i] = (char *)mem[0] + blocksize * i;
        memset(mem[i], 0, blocksize);
    }
    for(int i = 0; i < blocknr; i++)
        munmap(mem[i], blocksize);
    return NULL;
}

static int oshfs_getattr(const char *path, struct stat *stbuf)
{
    int ret = 0;
    struct filenode *node = get_filenode(path);         
    if(strcmp(path, "/") == 0)                          //if root dir
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
    struct filenode *node = root;
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
    struct stat st;                                                         
    st.st_mode = S_IFREG | 0644;                                            
    st.st_uid = fuse_get_context()->uid;
    st.st_gid = fuse_get_context()->gid;
    st.st_nlink = 1;                                                       
    st.st_size = 0;                                                        
    create_filenode(path + 1, &st);                                        
    return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)          
{
    return 0;
}

static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    unsigned int new_pieces, pre_size;
    int success;
    struct filenode *node = get_filenode(path);                     
    pre_size = node->st.st_size;
    if(offset + size > node->st.st_size)
        node->st.st_size = offset + size;                              
    new_pieces = (node->st.st_size % blocksize) ? ((int)node->st.st_size / blocksize + 1) : ((int)node->st.st_size / blocksize);
    success = my_realloc(node, new_pieces);          
    node->pieces = new_pieces;
    if (success != -1)
    {
        int buf_add = 0;
        int offset_block = (int)offset/blocksize;
        int new_offset = offset % blocksize;
        int i = 0;
        while(buf_add < size)
        {
        	int copy_size;
            if(i == 0)
            {
        	    copy_size = blocksize - new_offset;
        	    memcpy(mem[node->content[offset_block + i]] + new_offset, buf + buf_add, copy_size);                 
        	    buf_add += copy_size;
            }
            else
            {
            	if((size - buf_add) > blocksize)	copy_size = blocksize;
            	else	copy_size = size - buf_add;
             	memcpy(mem[node->content[offset_block + i]], buf + buf_add, copy_size);                  
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
    int success;
    struct filenode *node = get_filenode(path);             
    node->st.st_size = size;                               
    int new_pieces = (node->st.st_size % blocksize) ? ((int)node->st.st_size / blocksize + 1) : ((int)node->st.st_size / blocksize);
    success = my_realloc(node, new_pieces);           
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
      	    buf_add += copy_size;
        }
        else
        {
        	if((ret - buf_add) > blocksize)	copy_size = blocksize;
          	else	copy_size = ret - buf_add;
          	memcpy(buf + buf_add, mem[node->content[offset_block + i]], copy_size);                  
      	    buf_add += copy_size;
        }
        i++;
    }   
    return ret;                                                  
}

static int oshfs_unlink(const char *path)               
{
    struct filenode *node1 = get_filenode(path);
    struct filenode *node2 = root;
    if (node1==root)                       
    {
        root=node1->next;
        node1->next=NULL;
        for (int i = 0; i < node1->pieces; ++i)	my_free(node1->content[i]);
    	my_free(node1->logic_addr);
    	return 0;    
    }
    else if (node1)                         
    {
        while(node2->next!=node1&&node2!=NULL)
            node2 = node2->next;
        node2->next=node1->next;
        node1->next=NULL;
    	for (int i = 0; i < node1->pieces; ++i)	my_free(node1->content[i]);
    	my_free(node1->logic_addr);  
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
