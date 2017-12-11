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
int repairmode = 0;

if(strcmp(name,"-r") == 0){
	name = argv[2];
	repairmode = 1;
}

int fd = open(name, O_RDONLY);

if(fd < 0){
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
int i,j,k,l,m;
struct dinode *id;
id = (struct dinode *)(img_ptr + 2*BSIZE);
uint* indir_addr;
struct dirent *curr_dir;
int used_address[sb->size], used_bitmap_address[BPB];
int used_inode[sb->ninodes], ref_dir[sb->ninodes];
int file_ref_link[sb->ninodes], file_ref_dir_count[sb->ninodes];
int dir_ref[sb->ninodes], ddir_ref_count[sb->ninodes][sb->ninodes];
int is_dir[sb->ninodes], dir_parent[sb->ninodes];
memset(used_address, 0 , sizeof(used_address));
memset(used_bitmap_address, 0 , sizeof(used_bitmap_address));
memset(used_inode, 0,  sizeof(used_inode));
memset(ref_dir, 0, sizeof(ref_dir));
memset(file_ref_link, 0, sizeof(file_ref_link));
memset(file_ref_dir_count, 0, sizeof(file_ref_dir_count));
memset(dir_ref, 0, sizeof(dir_ref));
memset(ddir_ref_count, 0, sizeof(ddir_ref_count[0][0])*sb->ninodes*sb->ninodes);
memset(is_dir,0, sizeof(is_dir));
memset(dir_parent, 0, sizeof(dir_parent));

// marking the empty and superblock as used
used_address[0] = 1;
used_address[1] = 1;

for(m = 2; m <= (((sb->ninodes)*(sizeof(struct dinode)))/BSIZE) + 3 ; m++) {
used_address[m] = 1;
}
/*
for(i=0 ; i< sb->ninodes ; i++){
	if(id->type == 1)
		isdir[i] = 1;
	id++;
}
*/

id = (struct dinode *)(img_ptr + 2*BSIZE);
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
		used_inode[i] = 1; // marking used inode
		
		if((id->type == 2)) {
			file_ref_link[i] = id->nlink;
		}
		//checking for the validity of direct and indirect address 
		for(j = 0 ; j <= 12 ; j++)
		{
			if(j != 12) { 
				if((id->addrs[j]*BSIZE >= s.st_size) && (id->addrs[j] != 0)){
		 			fprintf(stderr,"ERROR: bad direct address in inode.\n");
					close(fd);
					exit(1);
				}
				//direct addresses used and marked free in bitmap 
				if(id->addrs[j] != 0 ){
					// check for . and .. dir entries for all directories
					// root is it's own parent (ie) .. entry of root should have inum == 1	
					if(id->type == 1){
						dir_ref[i] = 1;
						bool currcheck = false, parcheck = false;
						curr_dir = (struct dirent *)(img_ptr + id->addrs[j]*BSIZE);
						for(l= 0 ; l< 32 ; l++){
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
							if((strcmp(curr_dir->name,".") != 0) && (strcmp(curr_dir->name, "..") != 0))
								ddir_ref_count[i][curr_dir->inum]++;
							if(strcmp(curr_dir->name,"..") == 0)
								dir_parent[i] = curr_dir->inum;

							file_ref_dir_count[curr_dir->inum]++;
							ref_dir[curr_dir->inum] = 1;
							curr_dir++;
							
						}
						if((j == 0) && (!currcheck && !parcheck)) {
							fprintf(stderr, "ERROR: directory not properly formatted.\n"); 
							close(fd);
							exit(1);
						}

					}
					if(used_address[id->addrs[j]] == 0) used_address[id->addrs[j]] = 1; // marking used direct addresses
					else{	// dir address accessed more than once
						fprintf(stderr, "ERROR: direct address used more than once.\n");
						close(fd);
						exit(1);
					}
					uint b = id->addrs[j];
					char *bblk_addr = (char *)(img_ptr + BBLOCK(b,sb->ninodes)*BSIZE);
					uint bi = b%BPB;
					int m = 1 << (bi%8);
					if((bblk_addr[bi/8] & m) == 0){
						fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
						close(fd);
						exit(1);
					}
				}
			}
			else{	
				if(used_address[id->addrs[j]] == 0) used_address[id->addrs[j]] = 1;
				indir_addr = (id->addrs[j] != 0)? (uint *)(img_ptr + id->addrs[j]*BSIZE): 0;
				if(indir_addr != 0)
				for( k= 0 ; k< 128 ; k++){
					//printf("indirect add: %d offset: %d size : %ld \n" , *indir_addr, (*indir_addr)*BSIZE, s.st_size);
					if(((*indir_addr)*BSIZE >= s.st_size) && (*indir_addr != 0)){
						fprintf(stderr, "ERROR: bad indirect address in inode.\n");
						close(fd);
						exit(1);
					}
					//indirect addresses used and marked free in bitmap
					if(*indir_addr != 0 ){
						if(used_address[*indir_addr] == 0) used_address[*indir_addr] = 1;//marking used indirect address
						else {
							fprintf(stderr, "ERROR: direct address used more than once.\n");
							close(fd);
							exit(1);
						}
						uint b = *indir_addr;
						char *bblk_addr = (char *)(img_ptr + BBLOCK(b,sb->ninodes)*BSIZE);
						uint bi = b%BPB;
						int m = 1 << (bi%8);
						if((bblk_addr[bi/8] & m) == 0){
							fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
							close(fd);
							exit(1);
						}
							
						if(id->type == 1) {	
							curr_dir = (struct dirent *)(img_ptr + (*indir_addr)*BSIZE);
							for(l= 0 ; l< 32 ; l++){
								if((strcmp(curr_dir->name,".") != 0) && (strcmp(curr_dir->name, "..") != 0))
									ddir_ref_count[i][curr_dir->inum]++;
								
								file_ref_dir_count[curr_dir->inum]++;
								ref_dir[curr_dir->inum] = 1;
								curr_dir++;
							}
						}
						
							
					}
					indir_addr++; 
				}
			} 
		//	printf("type = %d ,%x %ld\n",id->type, (id->addrs[j]*BSIZE), s.st_size);

		}



	}
	id++;
}

int need_repair[sb->ninodes];
memset(need_repair, 0 , sizeof(need_repair));
bool repair = false;

for(int x=1 ; x < sb->ninodes ;x++){
	
	if((file_ref_link[x] != 0) && (file_ref_link[x] != file_ref_dir_count[x]) && (repairmode == 0)){
		fprintf(stderr,"ERROR: bad reference count for file.\n");
		close(fd);
		exit(1);
	}
	if((used_inode[x] == 1))
		if(ref_dir[x] != 1){
			if(repairmode == 1){
				if(repair == false) repair = true;
				need_repair[x] = 1;
			}
			else{
				fprintf(stderr,"ERROR: inode marked use but not found in a directory.\n");
				close(fd);
				exit(1);
			}
		
	}
	if((ref_dir[x] == 1))
		if(used_inode[x] == 0){
		fprintf(stderr,"ERROR: inode referred to in directory but marked free.\n");
		close(fd);
		exit(1);
	}
}

if(repair){

	// finding root inode
	id = (struct dinode *)(img_ptr + 2*BSIZE);
	id++; // points to the root inode
	int lost_found = 0;

	// finding lost_found inode
	curr_dir = (struct dirent *)(img_ptr + id->addrs[0]*BSIZE);
	for(int j =0 ; j<32; j++){
		if(strcmp(curr_dir->name,"lost_found") == 0) {
			lost_found = curr_dir->inum;
		}
		curr_dir++;
	}

	//marking lost_found directory with lost inode dir entries
	id = (struct dinode *)(img_ptr + 2*BSIZE);
	id = (id + lost_found); // lost found inode
	curr_dir = (struct dirent *)(img_ptr + id->addrs[0]*BSIZE);
	for(int j=0 ; j<32 ; j++){
		
		if((strcmp(curr_dir->name, ".") == 0)||(strcmp(curr_dir->name,"..")== 0)){
			//printf("lost_found : %d\n" , curr_dir->inum);
			curr_dir++; 
			continue;
		}

		for(int x = 0 ; x<sb->ninodes ; x++){
			if(need_repair[x] == 1){
				struct dirent *repairnode = (struct dirent *)malloc(sizeof(struct dirent));	
				repairnode->inum = x;
				sprintf(repairnode->name ,"repair%d", x);
				curr_dir = repairnode;
				
				if(dir_ref[x] == 1) { //lost_inode is a directory
					//printf("%d is a directory\n",x);
					struct dinode *rid = (struct dinode *)(img_ptr + 2*BSIZE);
					rid = (rid + x);
					struct dirent *rep_dir = (struct dirent *)(img_ptr + rid->addrs[0]*BSIZE);
					
					for(int z = 0 ; z < 32 ; z++){
						if(strcmp(rep_dir->name,"..") == 0){
							//printf("marking lost_found as parent for %d\n", x);
							rep_dir->inum = lost_found;
							break;
						}
						rep_dir++;
					}
					
				}
				need_repair[x] = 0; // mark repaired
				break;
			}

		}
//		printf("name : %s, inode number repaired : %d \n", curr_dir->name, curr_dir->inum);

		curr_dir++;
	}

}
else {

	for(int x=1; x < sb->ninodes ; x++) {
		bool more_than_once = false;
		if(dir_ref[x] == 1){
			for(int y=1 ; y< sb->ninodes; y++){
					if(x == y) continue;
					if(ddir_ref_count[y][x] == 1) {
						if(more_than_once == false)
							more_than_once = true;
						else{
							fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
							close(fd);
							exit(1);
						}
					}
			}
		}
	}



	char *bblk_addr = (char *) (img_ptr + BBLOCK(0,sb->ninodes)*BSIZE);
	for(int bi = 0 ; bi < sb->size ; bi++) {
		int m = 1 << (bi % 8);
		if(((bblk_addr[bi/8] & m) != 0) && (used_address[bi] == 0 )) {
			fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
			close(fd);
			exit(1);
		} 
	}
	//extra credits - parent directory mismatch
	for(int x = 1; x < sb->ninodes ; x++){
		if(dir_ref[x] == 1){
			int parent = dir_parent[x];
			if((x == 1) && (parent != x)){
				fprintf(stderr, "ERROR: parent directory mismatch.\n");	
				close(fd);
				exit(1);
			}
			else if((x > 1) &&(parent != 0)&&(ddir_ref_count[parent][x] == 0)){
				fprintf(stderr, "ERROR: parent directory mismatch.\n");	
				close(fd);
				exit(1);
			}
			//printf("node: %d, parent %d, reference by parent : %d\n", x, parent, ddir_ref_count[parent][x]); 
		}
	}

	for(int x=1 ; x< sb->ninodes ; x++){
		int used[sb->ninodes];
		memset(used, 0 , sizeof(used));
		if(dir_ref[x] ==1){
			used[x] = 1;
			int parent,temp;
			temp = x;
			do{
				parent = dir_parent[temp];
				//printf("node:%d , parent: %d\n", temp, parent);
				temp = parent;
				if(x != 1){
					if((parent!= 0)&& used[parent] == 0) used[parent] = 1;
					else{
						fprintf(stderr,"ERROR: inaccessible directory exists.\n");
						close(fd);
						exit(1);
					}
				}
			}while(parent != 1);

		}
	}

}

return 0;

}
