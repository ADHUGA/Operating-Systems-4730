#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
		int i, index, inode_index = 0;

		// load superblock, inodeMap, blockMap and inodes into the memory
		if(disk_mount(name) == 1) {
				disk_read(0, (char*) &superBlock);
				if(superBlock.magicNumber != MAGIC_NUMBER) {
						printf("Invalid disk!\n");
						exit(0);
				}
				disk_read(1, inodeMap);
				disk_read(2, blockMap);
				for(i = 0; i < numInodeBlock; i++)
				{
						index = i+3;
						disk_read(index, (char*) (inode+inode_index));
						inode_index += (BLOCK_SIZE / sizeof(Inode));
				}
				// root directory
				curDirBlock = inode[0].directBlock[0];
				disk_read(curDirBlock, (char*)&curDir);

		} else {
				// Init file system superblock, inodeMap and blockMap
				superBlock.magicNumber = MAGIC_NUMBER;
				superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
				superBlock.freeInodeCount = MAX_INODE;

				//Init inodeMap
				for(i = 0; i < MAX_INODE / 8; i++)
				{
						set_bit(inodeMap, i, 0);
				}
				//Init blockMap
				for(i = 0; i < MAX_BLOCK / 8; i++)
				{
						if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
						else set_bit(blockMap, i, 0);
				}
				//Init root dir
				int rootInode = get_free_inode();
				curDirBlock = get_free_block();

				inode[rootInode].type = directory;
				inode[rootInode].owner = 0;
				inode[rootInode].group = 0;
				gettimeofday(&(inode[rootInode].created), NULL);
				gettimeofday(&(inode[rootInode].lastAccess), NULL);
				inode[rootInode].size = 1;
				inode[rootInode].blockCount = 1;
				inode[rootInode].directBlock[0] = curDirBlock;

				curDir.numEntry = 1;
				strncpy(curDir.dentry[0].name, ".", 1);
				curDir.dentry[0].name[1] = '\0';
				curDir.dentry[0].inode = rootInode;
				disk_write(curDirBlock, (char*)&curDir);
		}
		return 0;
}

int fs_umount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
		int i, index, inode_index = 0;
		disk_write(0, (char*) &superBlock);
		disk_write(1, inodeMap);
		disk_write(2, blockMap);
		for(i = 0; i < numInodeBlock; i++)
		{
				index = i+3;
				disk_write(index, (char*) (inode+inode_index));
				inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// current directory
		disk_write(curDirBlock, (char*)&curDir);

		disk_umount(name);	
}

int search_cur_dir(char *name)
{
		// return inode. If not exist, return -1
		int i;

		for(i = 0; i < curDir.numEntry; i++)
		{
				if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
		}
		return -1;
}

int file_create(char *name, int size)
{
		int i;

		if(size > SMALL_FILE) {
				printf("Do not support files larger than %d bytes.\n", SMALL_FILE);
				return -1;
		}

		if(size < 0){
				printf("File create failed: cannot have negative size\n");
				return -1;
		}

		int inodeNum = search_cur_dir(name); 
		if(inodeNum >= 0) {
				printf("File create failed:  %s exist.\n", name);
				return -1;
		}

		if(curDir.numEntry + 1 > MAX_DIR_ENTRY) {
				printf("File create failed: directory is full!\n");
				return -1;
		}

		int numBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numBlock++;

		if(numBlock > superBlock.freeBlockCount) {
				printf("File create failed: data block is full!\n");
				return -1;
		}

		if(superBlock.freeInodeCount < 1) {
				printf("File create failed: inode is full!\n");
				return -1;
		}

		char *tmp = (char*) malloc(sizeof(int) * size + 1);

		rand_string(tmp, size);
		printf("New File: %s\n", tmp);

		// get inode and fill it
		inodeNum = get_free_inode();
		if(inodeNum < 0) {
				printf("File_create error: not enough inode.\n");
				return -1;
		}

		inode[inodeNum].type = file;
		inode[inodeNum].owner = 1;  // pre-defined
		inode[inodeNum].group = 2;  // pre-defined
		gettimeofday(&(inode[inodeNum].created), NULL);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		inode[inodeNum].size = size;
		inode[inodeNum].blockCount = numBlock;
		inode[inodeNum].link_count = 1;

		// add a new file into the current directory entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = inodeNum;
		printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
		curDir.numEntry++;

		// get data blocks
		for(i = 0; i < numBlock; i++)
		{
				int block = get_free_block();
				if(block == -1) {
						printf("File_create error: get_free_block failed\n");
						return -1;
				}
				//set direct block
				inode[inodeNum].directBlock[i] = block;

				disk_write(block, tmp+(i*BLOCK_SIZE));
		}

		//update last access of current directory
		gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);		

		printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

		free(tmp);
		return 0;
}

int file_cat(char *name)
{
		int inodeNum, i, size;
		char str_buffer[512];
		char * str;

		//get inode
		inodeNum = search_cur_dir(name);
		size = inode[inodeNum].size;

		//check if valid input
		if(inodeNum < 0)
		{
				printf("cat error: file not found\n");
				return -1;
		}
		if(inode[inodeNum].type == directory)
		{
				printf("cat error: cannot read directory\n");
				return -1;
		}

		//allocate str
		str = (char *) malloc( sizeof(char) * (size+1) );
		str[ size ] = '\0';

		for( i = 0; i < inode[inodeNum].blockCount; i++ ){
				int block;
				block = inode[inodeNum].directBlock[i];

				disk_read( block, str_buffer );

				if( size >= BLOCK_SIZE )
				{
						memcpy( str+i*BLOCK_SIZE, str_buffer, BLOCK_SIZE );
						size -= BLOCK_SIZE;
				}
				else
				{
						memcpy( str+i*BLOCK_SIZE, str_buffer, size );
				}
		}
		printf("%s\n", str);

		//update lastAccess
		gettimeofday( &(inode[inodeNum].lastAccess), NULL );

		free(str);

		//return success
		return 0;
}

int file_read(char *name, int offset, int size)
{
	int iNodeCounter;
	int space;
	int index;
	char* text;
	char BufferArray[BLOCK_SIZE];


	iNodeCounter = search_cur_dir(name); // Finds the specific inode


	if (iNodeCounter < 0) //Determines what we just found
	{
		printf("File cannot be found within this directory. Please try again!");
		printf("\n");
		return -1;
	}
	if (inode[iNodeCounter].type == directory)
	{
		printf("This is a directory not a file. Please try again!\n");
		printf("\n");
		return -1;
	}
	if (offset > inode[iNodeCounter].size)
	{
		printf("The starting position is larger than the size of the file itself. Please try again!");
		printf("\n");
		return -1;
	}
	if (offset < 0 || size < 0)
	{
		printf("The arguments are not valid for your read. Please try again!");
		printf("\n");
		return -1;
	}
	if (offset + size > inode[iNodeCounter].size)
	{
		printf("The arguments go beyond the limit of the file itself. Please try again!");
		printf("\n");
		return -1;
	}

	
	text = (char*)malloc(sizeof(char) * (size + 1)); //Setting the new local variable with malloc as defined in the sldies
	text[size] = '\0'; //Setting a size to our buffer


	space = offset / BLOCK_SIZE;
	index = 0;
	offset = offset % BLOCK_SIZE;

	while (index < size)
	{
		disk_read(inode[iNodeCounter].directBlock[space], BufferArray);

		if (offset + size - index > BLOCK_SIZE)
		{
			memcpy(text + index, BufferArray + offset, BLOCK_SIZE - offset); //Use of memcpy as requested in the slides
			index = BLOCK_SIZE + index - offset;
		}
		else
		{
			memcpy(text + index, BufferArray + offset, size - index); //Use of memcpy as requested in the slides
			index = size;
		}
		offset = 0;

		space = space + 1;
	}

	printf("%s\n", text);

	free(text);

	//update timestamp
	gettimeofday(&(inode[iNodeCounter].lastAccess), NULL);
	return 0;
}


int file_stat(char *name)
{
		char timebuf[28];
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file cat error: file is not exist.\n");
				return -1;
		}

		printf("Inode\t\t= %d\n", inodeNum);
		if(inode[inodeNum].type == file) printf("type\t\t= File\n");
		else printf("type\t\t= Directory\n");
		printf("owner\t\t= %d\n", inode[inodeNum].owner);
		printf("group\t\t= %d\n", inode[inodeNum].group);
		printf("size\t\t= %d\n", inode[inodeNum].size);
		printf("link_count\t= %d\n", inode[inodeNum].link_count);
		printf("num of block\t= %d\n", inode[inodeNum].blockCount);
		format_timeval(&(inode[inodeNum].created), timebuf, 28);
		printf("Created time\t= %s\n", timebuf);
		format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
		printf("Last acc. time\t= %s\n", timebuf);
}

int file_remove(char *name)
{
	int index;
	int iNodeCounter;

	iNodeCounter = search_cur_dir(name); // Finds the specific inode

	if (iNodeCounter < 0) //Determining what we found
	{
		printf("There is no file with this name in this directory. Please try again!");
		printf("\n");
		return -1;
	}
	if (inode[iNodeCounter].type != file)
	{
		printf("The name that you requested is not a file. Please try again!");
		printf("\n");
		return -1;
	}



	//Adjust directory entry array
	for (index = 0; index < inode[iNodeCounter].blockCount; index++)
	{
		set_bit(blockMap, inode[iNodeCounter].directBlock[index], 0); //This for loop free up the space of the data in the blocks.
		superBlock.freeBlockCount = superBlock.freeBlockCount + 1;
	}

	set_bit(inodeMap, iNodeCounter, 0);
	superBlock.freeInodeCount = superBlock.freeInodeCount + 1; //Gives us room for another inode


	for (index = 0; index < curDir.numEntry; index++)
	{
		if (curDir.dentry[index].inode == iNodeCounter)
			break;
	}
	for (index; index < curDir.numEntry - 1; index++) //These for loops move around the directory to make sure they are not shifted one place over
	{
		curDir.dentry[index] = curDir.dentry[index+1];
	}


	curDir.numEntry = curDir.numEntry - 1;  //Remove one entry from the directory
	gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL); //Updat the time of the last file access

	printf("Successfully removed the file %s\n", name);
	return 0;
}

int dir_make(char* name)
{
	int DirectoryStorage = get_free_block();
	int index;
	Dentry SecondDirectory;

	int iNodeCounter = search_cur_dir(name);

	if (iNodeCounter >= 0) 
	{

		printf("Cannot create this directory because the name is in use. Please try again!", name);
		printf("\n");
		return -1;
	}

	if (curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry))) 
	{
		printf("The directory is full. Please try again!");
		printf("\n");
		return -1;
	}

	if (superBlock.freeInodeCount < 1) 
	{
		printf("There are not enough inodes to create this directory. Please try again!");
		printf("\n");
		return -1;
	}

	iNodeCounter = get_free_inode();
	if (iNodeCounter < 0)
	{
		printf("There are not enough inodes to perfom this action. Please try again!\n");
		return -1;
	}

	inode[iNodeCounter].type = directory;
	inode[iNodeCounter].owner = 0;
	inode[iNodeCounter].group = 0;
	gettimeofday(&(inode[iNodeCounter].created), NULL);
	gettimeofday(&(inode[iNodeCounter].lastAccess), NULL);
	inode[iNodeCounter].size = 1;
	inode[iNodeCounter].blockCount = 1;
	inode[iNodeCounter].link_count = 1;
	inode[iNodeCounter].directBlock[0] = DirectoryStorage;
	SecondDirectory.numEntry = 2;
	strncpy(SecondDirectory.dentry[1].name, "..", 2);
	SecondDirectory.dentry[1].name[2] = '\0';
	SecondDirectory.dentry[1].inode = curDir.dentry[0].inode;
	strncpy(SecondDirectory.dentry[0].name, ".", 1);
	SecondDirectory.dentry[0].name[1] = '\0';
	SecondDirectory.dentry[0].inode = iNodeCounter;
	disk_write(DirectoryStorage, (char*)&SecondDirectory);

	// The directory gets copied and added into the currenty directory
	strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
	curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
	curDir.dentry[curDir.numEntry].inode = iNodeCounter;
	curDir.numEntry = curDir.numEntry + 1;
	disk_write(curDirBlock, (char*)&curDir);

	printf("Successfully created sub-directory %s under the current directory.", name);
	printf("\n");

	return 0;
}

int dir_remove(char *name)
{
	int position;
	int index;
	int BlockDirectory;
	int iNodeCounter;

	if (strcmp(".", name) == 0 | strcmp("..", name) == 0) 
	{
		printf("This directory is not eligble for deletion. Please try again!");
		printf("\n");
		return -1;
	}

	iNodeCounter = search_cur_dir(name);

	if (iNodeCounter >= 0) 
	{
		if (inode[iNodeCounter].type != directory)
		{
			printf("This is not a directory. Please try again!");
			printf("\n");
			return -1;
		}
		BlockDirectory = inode[iNodeCounter].directBlock[0];
		set_bit(blockMap, BlockDirectory, 0);
		superBlock.freeBlockCount = superBlock.freeBlockCount + 1;
		set_bit(inodeMap, iNodeCounter, 0);
		superBlock.freeInodeCount = superBlock.freeBlockCount + 1;
		for (index = 0; index < curDir.numEntry; index++)
		{
			if (strcmp(curDir.dentry[index].name, name) == 0) 
			{
				position = index;
				break;
			}
		}
		for (index = position; index < curDir.numEntry - 1; index++)
		{
			curDir.dentry[index].inode = curDir.dentry[index+1].inode;
			strcpy(curDir.dentry[index].name, curDir.dentry[index+1].name);
		}
		curDir.numEntry = curDir.numEntry - 1;
		disk_write(curDirBlock, (char*)&curDir);
		printf(" The directory %s has been successfully deleted.", name);
		printf("\n");
		return 0;
	}
	else 
	{
		printf("The directory cannot be found. Please try Again!");
		printf("\n");
		return -1;
	}
}

int dir_change(char* name)
{
	int index;
	int iNodeCounter;

	iNodeCounter = search_cur_dir(name);

	if (iNodeCounter >= 0) 
	{
		if (inode[iNodeCounter].type != directory) 
		{
			printf("Please select a DIRECTORY. Try again!\n");
			return -1;
		}
		curDirBlock = inode[iNodeCounter].directBlock[0];
		disk_read(curDirBlock, (char*)&curDir);
		return 0;
	}
	else 
	{
		printf("There is no directory of this name.");
		return -1;
	}
}

int ls()
{
		int i;
		for(i = 0; i < curDir.numEntry; i++)
		{
				int n = curDir.dentry[i].inode;
				if(inode[n].type == file) printf("type: file, ");
				else printf("type: dir, ");
				printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
		}

		return 0;
}

int fs_stat()
{
		printf("File System Status: \n");
		printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
}

int hard_link(char* src, char* dest)
{
	int index;
	int iNodeCounter;

	iNodeCounter = search_cur_dir(dest);


	if (iNodeCounter >= 0)
	{
		printf("The file %s already exists for hard link. Please try again!", dest);
		printf("\n");
		return -1;
	}

	iNodeCounter = search_cur_dir(src);

	if (iNodeCounter < 0)
	{
		printf("There is no file with this name in this directory. Please try again!\n", src);
		printf("\n");
		return -1;
	}
	if (curDir.numEntry + 1 >= (BLOCK_SIZE / sizeof(DirectoryEntry)))
	{
		printf("The directory is full. Please try again!");
		printf("\n");
		return -1;
	}
	if (superBlock.freeInodeCount < 1)
	{
		printf("There are not enough inodes to do this hard link. Please try again!");
		printf("\n");
		return -1;
	}


	inode[iNodeCounter].link_count = inode[iNodeCounter].link_count + 1; //How many times we have linked to this inode


	strncpy(curDir.dentry[curDir.numEntry].name, dest, strlen(dest));
	curDir.dentry[curDir.numEntry].name[strlen(dest)] = '\0';
	curDir.dentry[curDir.numEntry].inode = iNodeCounter;
	printf("The current directory is %s, The file name is %s\n", curDir.dentry[curDir.numEntry].name, dest);
	curDir.numEntry = curDir.numEntry + 1;
	disk_write(curDirBlock, (char*)&curDir); 
	//The current directory gets the new file

	printf("Successful hard link! %s, inode %d", dest, iNodeCounter);
	printf("\n");
	return 0;
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{

    printf ("\n");
	if(command(comm, "df")) {
				return fs_stat();

    // file command start    
    } else if(command(comm, "create")) {
        if(numArg < 2) {
            printf("error: create <filename> <size>\n");
            return -1;
        }
		return file_create(arg1, atoi(arg2)); // (filename, size)

	} else if(command(comm, "stat")) {
		if(numArg < 1) {
			printf("error: stat <filename>\n");
			return -1;
		}
		return file_stat(arg1); //(filename)

	} else if(command(comm, "cat")) {
		if(numArg < 1) {
			printf("error: cat <filename>\n");
			return -1;
		}
		return file_cat(arg1); // file_cat(filename)

	} else if(command(comm, "read")) {
		if(numArg < 3) {
			printf("error: read <filename> <offset> <size>\n");
			return -1;
		}
		return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);

	} else if(command(comm, "rm")) {
		if(numArg < 1) {
			printf("error: rm <filename>\n");
			return -1;
		}
		return file_remove(arg1); //(filename)

	} else if(command(comm, "ln")) {
		return hard_link(arg1, arg2); // hard link. arg1: src file or dir, arg2: destination file or dir

    // directory command start
	} else if(command(comm, "ls"))  {
		return ls();

	} else if(command(comm, "mkdir")) {
		if(numArg < 1) {
			printf("error: mkdir <dirname>\n");
			return -1;
		}
		return dir_make(arg1); // (dirname)

	} else if(command(comm, "rmdir")) {
		if(numArg < 1) {
			printf("error: rmdir <dirname>\n");
			return -1;
		}
		return dir_remove(arg1); // (dirname)

	} else if(command(comm, "cd")) {
		if(numArg < 1) {
			printf("error: cd <dirname>\n");
			return -1;
		}
		return dir_change(arg1); // (dirname)

	} else {
		fprintf(stderr, "%s: command not found.\n", comm);
		return -1;
	}
	return 0;
}

