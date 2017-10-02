// gcc -std=c11 -W -Wall -pedantic -Wvla -Werror cli_pipes.c -ocli_pipes.out -g

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/wait.h>

// the maximum number of pipe sections / command arguments
// less efficient but cleaner than invoking a bunch of structs of different types
#define BUFFSIZE 64

#define EXIT_STRING "exit\n"

// represents each piece between / before / after each pipe
typedef struct pipePieceStruct {
	char * inputArgs[BUFFSIZE];
	int tokenCount;
	int pipeFDarray[2]; // in / out file descriptor
	int childPid;
} pipePiece;


int main(void)
{
	char * inputLine = NULL;
	ssize_t bytesRead = 0;
	size_t mallocRef = 0; // just a reference for getline

	// there may be file input / output redirects later, so first save the current file descriptors
	const int SAVED_STDIN = dup(STDIN_FILENO);
	const int SAVED_STDOUT = dup(STDOUT_FILENO);

	write(STDOUT_FILENO, "> ", 2);

	while (((bytesRead = getline(& inputLine, & mallocRef, stdin)) != -1) && (strcmp(inputLine, EXIT_STRING) != 0))
	{
		// optional input and output files
		char inputSource[BUFFSIZE];
		inputSource[0] = 0;
		char outputDest[BUFFSIZE];
		outputDest[0] = 0;

		pipePiece pipePieceArray[BUFFSIZE];

		int pipePieceArraySize = 0; // set position of initial piece
		int inputArgsPostion = 0;

		char * inputTokenPtr = NULL;

		// initalize first struct
		pipePieceArray[pipePieceArraySize].tokenCount = 0;

		// tokenike line
		inputTokenPtr = strtok(inputLine, "\n ");
		while (inputTokenPtr != NULL)
		{
			if (inputTokenPtr[0] == '|') // pipe detected
			{
				// close current and move to new pipe piece
				pipePieceArray[pipePieceArraySize].inputArgs[inputArgsPostion] = NULL;
				pipePieceArray[pipePieceArraySize].tokenCount = inputArgsPostion;

				inputArgsPostion = 0;
				++pipePieceArraySize;

				// initalize struct
				pipePieceArray[pipePieceArraySize].tokenCount = 0;
			}
			else // add cli command or its arguments to pipe piece
			{
				pipePieceArray[pipePieceArraySize].inputArgs[inputArgsPostion] = inputTokenPtr;
				++inputArgsPostion;
			}

			inputTokenPtr = strtok(NULL, "\n ");
		}

		// set final null char for arguments
		pipePieceArray[pipePieceArraySize].inputArgs[inputArgsPostion] = NULL;
		pipePieceArray[pipePieceArraySize].tokenCount = inputArgsPostion;

		// spawn children and process args
		for (int i = 0; i <= pipePieceArraySize; ++i)
		{
			dup2(SAVED_STDIN, STDIN_FILENO); // reset input for new piece

			// open pipe
			if((pipePieceArraySize > 0) && (pipe(pipePieceArray[i].pipeFDarray) == -1))
			{
				printf("error opening pipe\n");
				perror("pipe");
				exit(1);
			}

			// create child pid for each pipe piece
			int pid;
			if ((pid = fork()) == -1) // error fork
			{
				perror("fork");
				write(STDOUT_FILENO, "fork error in first child\n", 11);
				exit(1);
			}
			else if (pid == 0) // child
			{
				// save child pid
				pipePieceArray[i].childPid = getpid();

				char * processArgs[BUFFSIZE] = {'\0'};
				size_t processArgsCount = 0;

				// assemble argument array for processing
				for (int j = 0; j < pipePieceArray[i].tokenCount; ++j)
				{
					if (pipePieceArray[i].inputArgs[j][0] == '<') // input redirect detected
					{
						// save input file name to inputSource
						strncpy(inputSource, & pipePieceArray[i].inputArgs[j][1], strlen(pipePieceArray[i].inputArgs[j]));
					}
					else if (pipePieceArray[i].inputArgs[j][0] == '>') // output redirect detected
					{
						// save output file name to outputDest
						strncpy(outputDest, & pipePieceArray[i].inputArgs[j][1], strlen(pipePieceArray[i].inputArgs[j]));
					}
					else // not a file redirect, must just be an argument
					{
						processArgs[processArgsCount] = pipePieceArray[i].inputArgs[j];
						++processArgsCount;
					}
				}

				// set piping redirects and execute
				if (pipePieceArraySize > 0) // pipes exists
				{
					// for each pid, read from the former pipe, write to the current pipe
					if (i == 0) // this pid is the first pipe
					{
						//if (strlen(inputSource) != 0) // input file exists
						if (inputSource != NULL)
						{
							int inputFD = open(inputSource, O_RDONLY);
							dup2(inputFD, STDIN_FILENO);

							close(inputFD); // testing
						}

						dup2(pipePieceArray[i].pipeFDarray[1], STDOUT_FILENO); // redirct output to pipe

						execvp(* processArgs, processArgs);
					}
					else if (i == pipePieceArraySize) // last pipe
					{
						dup2(pipePieceArray[i - 1].pipeFDarray[0], STDIN_FILENO); // read from previous pipe

						int outputFD = 0;

						if (outputDest != NULL)
						{
							outputFD = open(outputDest, O_CREAT | O_WRONLY | O_APPEND, 0660);
							dup2(outputFD, STDOUT_FILENO);
							close(outputFD);
						}
						else
						{
							dup2(SAVED_STDOUT, STDOUT_FILENO);
						}	

						execvp(* processArgs, processArgs);			
					}
					else // middle pipe
					{
						dup2(pipePieceArray[i - 1].pipeFDarray[0], STDIN_FILENO); // read from previous pipe
						dup2(pipePieceArray[i].pipeFDarray[1], STDOUT_FILENO); // redirct output to current pipe
						
						execvp(* processArgs, processArgs);
					}

					close(pipePieceArray[i].pipeFDarray[0]); // close current read pipe
					close(pipePieceArray[i].pipeFDarray[1]); // close current write pipe		
				}
				else // no pipe
				{
					if (strlen(inputSource) != 0) // input file detected
					{
						int inputFD = open(inputSource, O_RDONLY);
						dup2(inputFD, STDIN_FILENO);
					}

					int outputFD = 0;
					if (strlen(outputDest) != 0) // output file detected
					{
						outputFD = open(outputDest, O_CREAT | O_WRONLY | O_APPEND, 0660);
						dup2(outputFD, STDOUT_FILENO);
					}

					execvp(* processArgs, processArgs);

					// cleanup
					if (outputDest != NULL)
					{
						close(outputFD);
					}					
					dup2(SAVED_STDIN, STDIN_FILENO);
					dup2(SAVED_STDOUT, STDOUT_FILENO);
				}

			}
			else // parent
			{
				wait(0);

				if(pipePieceArraySize > 0)
				{
					// close(pipePieceArray[i].pipeFDarray[0]); // testing

					close(pipePieceArray[i].pipeFDarray[1]); // write complete, close write end
				}
			}	

		} // end spawn children


		dup2(SAVED_STDIN, STDIN_FILENO);
		dup2(SAVED_STDOUT, STDOUT_FILENO);
		printf("\n> ");

	} // end getline loop

} // end main