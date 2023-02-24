#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "webserver.h"
#include <semaphore.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_REQUEST 100

int port, numThread;
sem_t sem_empty; //Requirement of first semaphore
sem_t sem_full; //Requirement for second sempahore
sem_t mutex; //Semaphore mutex lock
int BufferArray[MAX_REQUEST];

int incoming, outgoing;
//bool thread = true;

void *listener()
{
	int r;
	struct sockaddr_in sin;
	struct sockaddr_in peer;
	int peer_len = sizeof(peer);
	int sock;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);
	r = bind(sock, (struct sockaddr *) &sin, sizeof(sin));
	if(r < 0) {
		perror("Error binding socket:");
		return;
	}

	r = listen(sock, 5);
	if(r < 0) {
		perror("Error listening socket:");
		return;
	}

	printf("HTTP server listening on port %d\n", port);

	//Need to initialize our semaphores
	sem_init(&sem_empty, 0, numThread); //0 means it is shared between theads of the process

	sem_init(&sem_full, 0, 0);

	sem_init(&mutex, 0, 1);



	while (1)
	{
		int s;
		s = accept(sock, NULL, NULL);
		if (s < 0) break;

		//process(s);
		sem_wait(&sem_empty);
		sem_wait(&mutex);
		BufferArray[++incoming] = s;
		if (incoming == MAX_REQUEST)
		{
			incoming = incoming * 0;
		}
		sem_post(&mutex);
		sem_post(&sem_full);
	}

	close(sock);
}

void *ConsumerThread()
{
	//thread = true;
	while (1)
	{
		int counter;
		sem_wait(&sem_full); //Decrementing
		sem_wait(&mutex); //Locking
		counter = BufferArray[++outgoing];
		if (outgoing == MAX_REQUEST)
		{
			outgoing = outgoing * 0;
		}
		sem_post(&mutex); //Unlocking
		sem_post(&sem_empty);
		process(counter);

	}
}

void thread_control()
{
	/* ----- */
	int index = 0;
	outgoing = 0;
	pthread_t PArray[numThread];
	pthread_t Plisten;
	incoming = 0;
	//bool run = true;

	pthread_create(&Plisten, NULL, listener, NULL); //To create the threads we will be using

	while (index < numThread)
	{
		PArray[index] = index;
		pthread_create(&PArray[index], NULL, ConsumerThread, NULL); //Created thread and passing with the consumer
		index = index + 1;
	}
	
	while (1)
	{
		for (index = 0; index < numThread; ++index)
		{
			if (pthread_join(PArray[index], NULL) == 0) //Assuming thread will always be able to connect
			{
				pthread_create(&PArray[index], NULL, ConsumerThread, NULL);
			}
		}
	}
}

int main(int argc, char *argv[])
{
	if(argc != 3 || atoi(argv[1]) < 2000 || atoi(argv[1]) > 50000)
	{
		fprintf(stderr, "./webserver_multi PORT(2001 ~ 49999) #_of_threads\n");
		return 0;
	}

	int i;
	port = atoi(argv[1]);
	numThread = atoi(argv[2]);
	thread_control();
	sem_destroy(&sem_empty);
	sem_destroy(&mutex);
	sem_destroy(&sem_full);
	return 0;
}
