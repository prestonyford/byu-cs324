#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main(int argc, char *argv[]) {
	int pid;

	printf("Starting program; process has pid %d\n", getpid());

	FILE* file = fopen("fork-output.txt", "w+");
	int filedesc = fileno(file);
	fprintf(file, "BEFORE FORK (%d)\n", filedesc);
	fflush(file);

	int pipefd[2];
	int err = pipe(pipefd);

	if ((pid = fork()) < 0) {
		fprintf(stderr, "Could not fork()");
		exit(1);
	}

	/* BEGIN SECTION A */

	printf("Section A;  pid %d\n", getpid());
	//sleep(5);

	/* END SECTION A */
	if (pid == 0) {
		/* BEGIN SECTION B */

		printf("Section B\n");
		//sleep(30);
		//sleep(30);
		//printf("Section B done sleeping\n");
		//sleep(5);
		fprintf(file, "SECTION B (%d)\n", filedesc);

		close(pipefd[0]);
		sleep(5); //10
		ssize_t written = write(pipefd[1], "Hello from Section B\n", 22);
		sleep(5); //10
		close(pipefd[1]);

		char *newenviron[] = { NULL };

		//
        	printf("Program \"%s\" has pid %d. Sleeping.\n", argv[0], getpid());
		//sleep(30);

        	if (argc <= 1) {
                	printf("No program to exec.  Exiting...\n");
                	exit(0);
        	}

        	printf("Running exec of \"%s\"\n", argv[1]);
		dup2(filedesc, 1);
        	execve(argv[1], &argv[1], newenviron);
	        printf("End of program \"%s\".\n", argv[0]);

		exit(0);

		/* END SECTION B */
	} else {
		/* BEGIN SECTION C */

		printf("Section C\n");
		//int wstatus;
                //wait(&wstatus); //Added line by me
		//sleep(30);
		//printf("Section C done sleeping\n");
		fprintf(file, "SECTION C (%d)\n", filedesc);
		fclose(file);
		//sleep(5);

		close(pipefd[1]);
		char buff[100];
		ssize_t bytes_read = read(pipefd[0], buff, 100);
		buff[bytes_read] = '\0';
		printf("Read %ld bytes:\n", bytes_read);
		printf("%s", buff);

		bytes_read = read(pipefd[0], buff, 100);
                buff[bytes_read] = '\0';
                printf("Read %ld bytes:\n", bytes_read);
                printf("%s", buff);

		close(pipefd[0]);

		exit(0);

		/* END SECTION C */
	}
	/* BEGIN SECTION D */

	printf("Section D\n");
	//sleep(30);

	/* END SECTION D */
}

