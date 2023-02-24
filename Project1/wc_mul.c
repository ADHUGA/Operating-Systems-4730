/*************************************************
	* C program to count no of lines, words and 	 *
	* characters in a file.			 *
	*************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>


#define MAX_PROC 100
#define MAX_FORK 1000

typedef struct count_t {
    int linecount;
    int wordcount;
    int charcount;
} count_t;


typedef struct plist_t {
		int capacity;
		int pid;
		int offset;
        int communication;
		//We could use int pipfd[2] to communicate with parent
} plist_t;

int CRASH = 0;

count_t word_count(FILE* fp, long offset, long size)
{
		char ch;
		long rbytes = 0;
		int wordCounter = 0;

		count_t count;
		// Initialize counter variables
		count.linecount = 0;
		count.wordcount = 0;
		count.charcount = 0;

		printf("[pid %d] reading %ld bytes from offset %ld\n", getpid(), size, offset);

		if(fseek(fp, offset, SEEK_SET) < 0) {
				printf("[pid %d] fseek error!\n", getpid());
		}

		while ((ch=getc(fp)) != EOF && rbytes < size) {
				// Increment character count if NOT new line or space
				if (ch != ' ' && ch != '\n') { ++count.charcount; }

				// Increment word count if new line or space character
				if (ch == ' ' || ch == '\n') {++count.wordcount;++wordCounter; }

				// Increment line count if new line character
				if (ch == '\n') { ++count.linecount;}
				rbytes++;
		}

		srand(getpid());
		if(CRASH > 0 && (rand()%100 < CRASH))
		{
				printf("[pid %d] crashed.\n", getpid());
				abort();
		}

		return count;
}

int main(int argc, char **argv)
{
		long fsize;
		FILE *fp;
		int numJobs;
		int i, pid, status;
		int nFork = 0;
		plist_t plist[MAX_PROC];
		count_t total, count;
		long childSize;
		long offset = 0;
		int fd[MAX_PROC][2];
		int counter = 0;

		if(argc < 3) {
				printf("usage: wc <# of processes> <filname>\n");
				return 0;
		}
		if(argc > 3) {
				CRASH = atoi(argv[3]);
				if(CRASH < 0) CRASH = 0;
				if(CRASH > 50) CRASH = 50;
		}
		printf("CRASH RATE: %d\n", CRASH);


		numJobs = atoi(argv[1]);
		if(numJobs > MAX_PROC) numJobs = MAX_PROC;

		total.linecount = 0;
		total.wordcount = 0;
		total.charcount = 0;

		// Open file in read-only mode
		fp = fopen(argv[2], "r");

		if(fp == NULL) {
				printf("File open error: %s\n", argv[2]);
				printf("usage: wc <# of processes> <filname>\n");
				return 0;
		}

		fseek(fp, 0L, SEEK_END);
		fsize = ftell(fp);
		fclose(fp);
		// calculate file offset and size to read for each child
		childSize = fsize / numJobs;
		for(i = 0; i < numJobs; i++) 
		{
                offset=i* childSize;
				if (i == numJobs - 1)
				{
					childSize = fsize - (numJobs - 1) * childSize;
				}
				//set pipe;
				if (pipe(fd[i]) == -1)
				{
					printf("Could not create the pipe!\n");
				}
				if(nFork++ > MAX_FORK) return 0;
				pid = fork();
				if(pid < 0) 
				{
						printf("Fork failed.\n");
				} else if(pid == 0) // Child
				{
						
						fp = fopen(argv[2], "r");
						// count = word_count(fp, 0, fsize)
						count = word_count(fp, offset, childSize);
						close(fd[i][0]);
						write(fd[i][1],&count,sizeof(count_t));
						close(fd[i][1]);
						// send the result to the parent through pipe
						fclose(fp);
						return 0;
				}
				if(pid>0)
				{
                    plist[i].capacity = childSize;
					plist[i].offset = offset;
					plist[i].communication = i;
					plist[i].pid = pid;
				}
		}

while(counter<numJobs)
{
	// Parent
// wait for all children
// check their exit status
// read the result of normalliy terminated child
// re-create new child if there is one or more failed child
// close pipe
    pid=wait(&status);
    i=0;
    while(pid!=plist[i].pid)
	{
            i = i + 1;
    }
            if(WIFEXITED(status))
			{
            counter = counter + 1;
            close(fd[plist[i].communication][1]);
            read(fd[plist[i].communication][0],&count,sizeof(count_t));
            close(fd[plist[i].communication][0]);
            total.charcount=total.charcount+count.charcount;
            total.linecount=total.linecount+count.linecount;
            total.wordcount=total.wordcount+count.wordcount;
			}
			else
			{
				if (pipe(fd[plist[i].communication]) == -1)
				{
					printf("Could not create the pipe!\n");
				}
			nFork=0;
			if (nFork++ > MAX_FORK)
			{
				return 0;
			}
			pid=fork();
			//child
			if(pid==0)
			{
				fp = fopen(argv[2], "r");
				count = word_count(fp, plist[i].offset, plist[i].capacity);
				close(fd[plist[i].communication][0]);
				write(fd[plist[i].communication][1],&count,sizeof(count_t));
				close(fd[plist[i].communication][1]);
				// send the result to the parent through pipe
				fclose(fp);
				return 0;
			}    
			else
			{
				plist[i].pid=pid;
			}
    }
}

		printf("\n========== Final Results ================\n");
		printf("Total Lines : %d \n", total.linecount);
		printf("Total Words : %d \n",total.wordcount);
		printf("Total Characters : %d \n", total.charcount);
		printf("=========================================\n");
		return(0);
}

