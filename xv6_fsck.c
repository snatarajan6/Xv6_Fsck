#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
// On-disk file system format.
// Both the kernel and user programs use this header file.

// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint addrs[NDIRECT+1];   // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i)     ((i) / IPB + 2)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block containing bit for block b
#define BBLOCK(b, ninodes) (b/BPB + (ninodes)/IPB + 3)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

int
main(int argc, char *argv[])
{

if(argc < 2){
	fprintf(stderr, "Usage: xv6_fsck <file_system_image>\n");
	return 1;
	}
char* name = argv[1];
int fd = open(name, O_RDONLY);

if(fd == -1){
	fprintf(stderr, "image not found.\n"); 
	exit(1);
	}
int rc;
struct stat s;
rc = fstat(fd,&s);
assert(rc == 0);

void *img_ptr = mmap(NULL, s.st_size, PROT_READ, MAP_PRIVATE,fd, 0);
assert(img_ptr != MAP_FAILED);
struct superblock *sb;
sb = (struct superblock *)(img_ptr + BSIZE);

//printf("%d %d %d \n", sb->size, sb->nblocks, sb->ninodes);
int i,j,k,l;
struct dinode *id;
id = (struct dinode *)(img_ptr + 2*BSIZE);
uint* indir_addr;
struct dirent *curr_dir;

for(i= 0;i< sb->ninodes; i++){
	
	// bad inode  
	if((id->type != 0) && (id->type != 1) && (id->type != 2) && (id->type != 3)){
		 fprintf(stderr,"ERROR: bad inode.\n");
		 close(fd);
		 exit(1);
	}

	// root directory exists
	if(((i == 1) && (id->type != 1))){ 
		fprintf(stderr, "ERROR: root directory does not exist.\n");
		close(fd);
		exit(1);
	}	
						
	
	// in-use inodes
	if(id->type != 0)
	{	
		// check for . and .. dir entries for all directories
		// root is it's own parent (ie) .. entry of root should have inum == 1	
		if(id->type == 1){
			bool currcheck = false, parcheck = false;
			curr_dir = (struct dirent *)(img_ptr + id->addrs[0]*BSIZE);
			for(l= 0 ; l<32 ; l++){
				if((strcmp(curr_dir->name,".") == 0) && (curr_dir->inum == i)){
					currcheck = true;	
				}
				if((strcmp(curr_dir->name,"..") == 0)){
					if((i==1) && (curr_dir->inum != 1)){
						fprintf(stderr, "ERROR: root directory does not exist.\n");
						close(fd);
						exit(1);
					}
						
					parcheck = true;
				}
				curr_dir++;
				
			}
			if(!currcheck && !parcheck) {
				fprintf(stderr, "ERROR: directory not properly formatted.\n"); 
				close(fd);
				exit(1);
			}

		}

		//checking for the validity of direct and indirect address ( 0 or < st.size) 
		for(j = 0 ; j <= 12 ; j++)
		{
			if(j != 12) { 
				if((id->addrs[j]*BSIZE > s.st_size) && (id->addrs[j] != 0)){
		 			fprintf(stderr,"ERROR: bad direct address in inode.\n");
					close(fd);
					exit(1);
				}
			}
			else{
				indir_addr = (id->addrs[j] != 0)? (uint *)(img_ptr + id->addrs[j]*BSIZE): 0;
				if(indir_addr != 0)
				for( k= 0 ; k< 128 ; k++){
					//printf("indirect add: %d offset: %d size : %ld \n" , *indir_addr, (*indir_addr)*BSIZE, s.st_size);
					if(((*indir_addr)*BSIZE >= s.st_size) && (*indir_addr != 0)){
						fprintf(stderr, "ERROR: bad indirect address in inode.\n");
						close(fd);
						exit(1);
					}

					indir_addr++; 
				}
			} 
			//printf("type = %d ,%x %ld\n",id->type, (id->addrs[j]*BSIZE), s.st_size);
		}



	}
	id++;
}


return 0;

}
