/*
  This program takes in an input from the user parses it using 
  tokenization. These tokens are placed into an array of strings. This array 
  of strings is then executed using a child process. Piping is supported, 
  please note only one pipe is allowed. This shell is also capable of 
  changing directory and setting the PS1 environmental variable. I/O 
  redirection is supported and signals such as SIGINT and SIGQUIT are handled
  accordingly. Please note i/o redirection with pipes is not supported at this time.
 */

//Header files and Marcos
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>

//Forward declarations
void die(const char* mistake);
char** parseInput(char* currentInput, char* currentDelim);
int executeArgs(char** currentArgs, char* newInput, char* newOutput, 
		int toAppend);
int executePipe(char** firstArgs, char** secondArgs);
int checkForBuiltIn(char** currentArgs);
int redirectFilter(char** currentArgs);
void sigBuster(int signum);
void handlerAtExe(int signum);
void handlerAtPipe(int signum);

//Global Constant
#define BUFFER 64 //Inital size of buffer

//Environment variables
extern char **environ;

//Destination buffer jump struct
jmp_buf Dest_Buffer, Dest_Buffer2, Dest_Buffer3;

/*
  The main function will print out a prompt and read in the user's input. 
  Afterwards the function to parse and execute these args is called. 
  userInput is a string and userArgs is an array of strings. The shellActive 
  variable is used to determine whenever the shell should remain active or 
  not. The PS1 variable is set here as well. If pipe characters are detected 
  the executePipe() function is called. If any i\o redirections symbols are 
  detected, the UserArgs array will take a detour through redirectFilter().
  If SIGINT or SIGQUIT signals are sent in this functions, the shell will 
  ignore it and jump to the beginning of main(). 
*/
int main(int argc, char* argv[])
{
  //If an signals are sent, the flow of execution is send back to here.
  sigsetjmp(Dest_Buffer, 1);
  
  //Catches SIGINT and SIGQUIT signals.
  signal(SIGINT, sigBuster);
  signal(SIGQUIT, sigBuster);

  char* userInput = NULL;
  char** arrOfCmd = NULL;
  char** userArgs = NULL;
  char** userArgs2 = NULL;
  int shellActive = 1;
  char* hasPipe = NULL;
  char* hasInput = NULL;
  char* hasOutput = NULL;
  
  //Sets the PS1 variable with a default value.
  setenv("PS1", "Leo's_Shell> ", 0);
  
  //The loop that runs Leo's shell.
  while(shellActive != 0)
    {
      printf("%s", getenv("PS1"));
      
      //The user's input is read in using getline().
      userInput = NULL;
      size_t bufspace = 0;
      ssize_t readIn = 0;
      readIn = getline(&userInput, &bufspace, stdin);
      
      //If getline() fails, the following will release the 
      //the memory allocated and kill the program.
      if(readIn == -1)
	{
	  free(userInput);
	  die("The function getline() failed.");
	}
      
      //Checks if the user typed in piped commands or any i/o redirections.
      hasPipe = strchr(userInput, '|');
      hasInput = strchr(userInput, '<');
      hasOutput = strchr(userInput, '>');
      
      //Changes the PS1 variable using setenv().
      if(userInput[0] == 'P' && userInput[1] == 'S' && userInput[2] == '1')
	{
	  userArgs = parseInput(userInput, "=\n");
	  setenv("PS1", userArgs[1], 1);
	}
      
      else if(hasPipe != NULL)
	{
	  //Divides up the input into commands.
	  arrOfCmd = parseInput(userInput, "|");

	  //Divides up the commands into words to be executed.
	  userArgs = parseInput(arrOfCmd[0], " \n");
	  userArgs2 = parseInput(arrOfCmd[1], " \n");
	  
	  //Executes the piped commands.
	  shellActive = executePipe(userArgs, userArgs2);
	  
	  sigsetjmp(Dest_Buffer3, 1);
	  
	  //Frees up all memory used here.
	  free(arrOfCmd);
	  free(userArgs);
	  free(userArgs2);
	}
      
      //For single, non piped commands.
      else
	{
	  //The string userInput is parsed.
	  userArgs = parseInput(userInput, " \n");
	  
	  //If any i/o redirection characters are detected the arguments
	  //are send through a detour known as redirectFilter().
	  if(hasInput != NULL || hasOutput != NULL)
	    {
	      shellActive = redirectFilter(userArgs);
	      if(userArgs != NULL) free(userArgs);
	    }
	  else
	    {
	      //Theses parsed arguments are then executed.
	      shellActive = executeArgs(userArgs, NULL, NULL, 0);
	      
	      //If a signal is send, after memory has been allocated
	      //the flow will jump back to here.
	      sigsetjmp(Dest_Buffer2, 1);
	      
	      //Frees up memory allocated to both these variables.
	      if(userArgs != NULL) free(userArgs);
	    }
	}
      
      //And this one too.
      if(userInput != NULL) free(userInput);
    }
  return 0;
}

/*
  If an error happens this function would catch it, spit out an error message
  using perror and kill the program.
*/
void die(const char* mistake)
{
  if(errno)
    perror(mistake);
  
  else
    printf("ERROR: %s\n", mistake);
  
  exit(1);
}

/*
  This function takes in a string and splits its up based on characters " " 
  and "\n". The arguments are tokenized into an array of strings. This array 
  is then returned to main() to be executed. The delimiters are determined by 
  the main function. SIGINT and SIGQUIT are handled accordingly.
*/
char** parseInput(char* currentInput, char* currentDelim)
{
  //signal handlers
  signal(SIGINT, handlerAtExe);
  signal(SIGQUIT, handlerAtExe);
  
  //Sets an arbitrary size for the buffer needed for 
  //the tokens. CurrentToks is a array of tokens.
  int currentBuf = BUFFER;
  char** currentToks = malloc(currentBuf * sizeof(char*));
  
  //Kills program if memory wasn't allocated for currentToks.
  if(!currentToks)
    die("Memory wasn't allocated for currentToks.");
  
  //Tokenizes the strings its delimiters.
  char* token = strtok(currentInput, currentDelim);
  
  //For loop fills up the currentToks with arguments.
  int index; 
  for(index = 0; token != NULL; index++)
    {
      /*
	If there is more arguments than the amount of	
	memory allocated, this statement would request
	more memory for currentToks.
      */
      if(index >= currentBuf)
	{
	  currentBuf += BUFFER;
	  currentToks = realloc(currentToks, currentBuf * sizeof(char*));
	  
	  //Checks if memory was reallocated for currentToks.
	  if(!currentToks)
	    die("Couldn't realloc memory for currentToks.");
	}
      
      //The token is placed inside currentToks and
      //moves onto the next token.
      currentToks[index] = token;
      
      token = strtok(NULL, currentDelim);
    }
  currentToks[index] = NULL;
  
  return currentToks;
}

/*
  executeArgs() would execute the arguments inside userArgs from the main 
  function. It would first check if there are any arguments at all then 
  check to see if "exit" or "cd" was typed in. Afterwards a separate process 
  would be forked and this child process would execute the command. In the 
  meantime the parent process waits for the child to finish. If any i/o 
  redirections commands are detected, stdin or stdout will be redirected
  here. SIGINT and SIGQUIT are handled accordingly.
*/
int executeArgs(char** currentArgs, char* newInput, char* newOutput, 
		int toAppend)
{
  //signal handlers
  signal(SIGINT, handlerAtExe);
  signal(SIGQUIT, handlerAtExe);

  int childState; //To be used for waitpid.
  
  //Checks if any arguments was typed in.
  if(currentArgs[0] == NULL) return 1;
  
  int hasBuiltin = checkForBuiltIn(currentArgs);
  
  if(hasBuiltin != 2) return hasBuiltin;
  
  //forks a child process and executes the command.
  pid_t pid = fork();
  
  //The child process.
  if (pid == 0) 
    {
      //Opens file for input.
      if(newInput != NULL)
	{
	  int input = open(newInput, O_RDONLY);
	  dup2(input, STDIN_FILENO);
	  close(input);
	}
      
      //Opens file for output.
      if(newOutput != NULL)
	{ 
	  int output;
	  //If ">>" is used instead of ">", append to file.
	  if(toAppend == 1)
	    {
	      output = open(newOutput, O_WRONLY | O_APPEND | O_CREAT,
			    S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	    }
	  //If ">" is used, the output file is overwritten.
	  else
	    {
	      output = open(newOutput, O_WRONLY | O_TRUNC | O_CREAT,
			    S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	    }

	  dup2(output, STDOUT_FILENO);
	  close(output);
	}

      //Executes the command!!! And checks if it fails.
      if(execvp(currentArgs[0], currentArgs) == -1) die("execvp failed");
    }
  
  //If there is an error forking, this statement
  //would kill the program.
  else if (pid < 0) die("fork() failed.");
  
  //The parent process waits.
  else
    if(waitpid(pid, &childState, 0) == -1) die("waitpid() failed"); 
    //For future assignments.
  
  //Continues the loop in main().
  return 1;
}

/*
  Here is where the piped commands will be executed. The function first 
  checks to see to see if there are two commands and then checks for any 
  built in commands. Built in commands cannot be used as piped commands in 
  this shell. The parent process forks two child processes, one for each 
  command. The parent later waits for both of its children. SIGINT and SIGQUIT
  are handled accordingly.
*/
int executePipe(char** firstArgs, char** secondArgs)
{
  //signal handlers
  signal(SIGINT, handlerAtPipe);
  signal(SIGQUIT, handlerAtPipe);

  //Checks for two commands.
  if(firstArgs[0] == NULL || secondArgs[0] == NULL)
    {
      printf("Expecting commands around pipe.\n");
      return 1;
    }
  
  //Checks for built in commands on either side of the pipe.
  int hasBuiltin = checkForBuiltIn(firstArgs);
  if(hasBuiltin != 2)
    {
      printf("Built in commands not allowed with pipe.\n");
      return 1;
    }
  //Checks second command.
  hasBuiltin = checkForBuiltIn(secondArgs);
  if(hasBuiltin != 2)
    {
      printf("Built in commands not allowed with pipe.\n");
      return 1;
    }
  
  int pipefd[2];
  pid_t pid, pid2;
  int childState, childState2;
  
  //Creates pipe.
  if(pipe(pipefd) == -1) die("pipe() failed.");
  
  //Forks first child.
  if ((pid = fork()) == -1) die("First fork() failed.");
  
  if (pid == 0)
    {
      //Sets up pipe to read. Playing around with "failed, line x" here.
      if(close(1) == -1) die("close() failed");     
      if(dup(pipefd[1]) == -1) die("dup() failed.");
      if(close(pipefd[0]) == -1) die("close() failed"); 
      if(close(pipefd[1]) == -1) die("close() failed");
      //Executes first command
      if(execvp(firstArgs[0], firstArgs) == -1) 
	die("First execvp() failed.");
    }
  
  //If still the parent fork second child.
  else if(pid > 0)
    {
      if ((pid2 = fork()) == -1) die("Second fork() failed.");
      
      //If second child.....
      if(pid2 == 0) 
	{
	  //Sets up pipe to write.
	  if(close(0) == -1) die("close() failed");  
	  if(dup(pipefd[0]) == -1) die("dup() failed.");
	  if(close(pipefd[1]) == -1) die("close() failed");
	  if(close(pipefd[0]) == -1) die("close() failed");
	  //Executes second command.
	  if(execvp(secondArgs[0],secondArgs) == -1) 
	    die("2nd execvp failed.");
	}
      else
	{
	  //Closes the pipe, and waits for both processes.
	  if(close(pipefd[0]) == -1) die("close() failed");
	  if(close(pipefd[1]) == -1) die("close() failed");
	  if(waitpid(pid, &childState, 0) == -1) 
	    die("waitpid() failed");
	  if(waitpid(pid2, &childState2, 0) == -1) 
	  die("waitpid() failed"); 
	}
    }
  else 
    {
      //Closes the pipe.
      if(close(pipefd[0]) == -1) die("close() failed");
      if(close(pipefd[1]) == -1) die("close() failed");
    }
  return 1;
}

/*
  Simple little function to check for built in commands. These include 
  "exit" and "cd".
*/
int checkForBuiltIn(char** currentArgs)
{
  //Checks for "exit" and exits the shell.
  if(strcmp(currentArgs[0], "exit") == 0) return 0; 
  
  //Checks for "cd" and changes directory.
  if(strcmp(currentArgs[0], "cd") == 0)
    {
      //Check if a path was specified by the user.
      if(currentArgs[1] == NULL) {
	if(chdir(getenv("HOME")) != 0) die("chdir() failed");
      }
      
      //Changes directory and checks for error.
      else if(chdir(currentArgs[1]) != 0) die("chdir() failed");
      
      return 1;
    }
  return 2;
}
/*
  This is the detour that userArgs will take if any i/o redirection characters
  are detected. I/o redirection characters and the file path will be stripped 
  off here. A new set of an array of arguments free of i/o redirections and 
  files will be assembled and then send to executeArgs() to be executed. An 
  extra argument is sent into executeArgs(), this arguments tells the function
  the user wants to append to a file not overwrite it. SIGINT and SIGQUIT are 
  handled here as well accordingly.
 */
int redirectFilter(char** currentArgs)
{
  //signal handlers
  signal(SIGINT, handlerAtExe);
  signal(SIGQUIT, handlerAtExe);
  
  char* inputFile = NULL;
  char* outputFile = NULL;
  int currentBuf = BUFFER;
  int newIndex = 0;
  int exitValue = 1;
  int toAppend = 3;
  
  //Creates a new array of arguments that will be executed later on.
  char** newArgs = malloc(currentBuf * sizeof(char*));

  //Checks for "<" and its file path.
  for(int index = 0; currentArgs[index] != NULL; index++)
    {
      if(strcmp(currentArgs[index], "<") == 0)
	{
	  //"<" is ignored but the path is saved into inputFile.
	  inputFile = currentArgs[index + 1]; 
	  index++;  //Skips past the file path.
	  continue;
	}

      //Checks for ">" and its file path.
      if(strcmp(currentArgs[index], ">") == 0)
	{
	  //">" is ignored and the path is saved into outputFile.
	  outputFile = currentArgs[index + 1];
	  index++; //Skips past the file path.
	  continue;
	}
      
      //Checks for ">>" and its file path.
      if(strcmp(currentArgs[index], ">>") == 0)
	{
	  //">>" is ignored and the path is saved.
	  outputFile = currentArgs[index + 1];
	  index++; //Skips past the file path.
	  toAppend = 1; //Tells executeArgs() that output is to be append.
	  continue;
	}

      //If the newIndex goes past the original size of new argument array.
      if(newIndex >= currentBuf)
	{
	  currentBuf += BUFFER; //If bigger, realloc more memory.
	  newArgs = realloc(newArgs, currentBuf * sizeof(char*));
	}
      
      else
	{ //Else just insert a new value into the new array.
	  newArgs[newIndex] = currentArgs[index];
	  newIndex++;
	}
    }
  
  //Return value is saved and send back to main but first.....
  exitValue = executeArgs(newArgs, inputFile, outputFile, toAppend);
  free(newArgs); //..... newArgs has to be freed.
  return exitValue;
}

/*
  This signal handler, handles any signals send before any memory is 
  allocated.
*/
void sigBuster(int signum)
{
  //signal handlers
  signal(SIGINT, sigBuster);  
  signal(SIGQUIT, sigBuster);
  
  //If SIGINT is caught.
  if(signum == SIGINT)
      printf("\nOw...\n");
  
  //If SIGQUIT is caught.
  if(signum == SIGQUIT)
      printf("\nAWW, okay that one hurt\n");
  
  //Jumps back to the beginning of main().
  siglongjmp(Dest_Buffer, 1);
}

/*
  This signal handler is used if any signals were recieved after memory
  has been allocated. Unlike the handler above, this one will allow the user
  to call ^C or ^\ on commands and stop them.
*/
void handlerAtExe(int signum)
{
  //signal handlers
  signal(SIGINT, handlerAtExe);  
  signal(SIGQUIT, handlerAtExe);
  
  //Tells user the command has been cancelled.
  printf("\n\nCommand cancelled===================\n\n");
  
  //Jumps back to where memory is freed in main().
  siglongjmp(Dest_Buffer2, 1);
}

/*
  Same as above but for piped commands.
*/
void handlerAtPipe(int signum)
{
  //signal handlers
  signal(SIGINT, handlerAtExe);  
  signal(SIGQUIT, handlerAtExe);
  
  //Tells user the command has been cancelled.
  printf("\n\nPiped Command cancelled==============\n\n");
  
  //Jumps back to where memory is freed in main() for piped commands.
  siglongjmp(Dest_Buffer3, 1);
}


