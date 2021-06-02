/* Author: Paul Newling
*  Class: OSU CS344 W2021
*  Program: Assignment 3: Smallsh
*/

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

//definitions given by assignment
#define MAXINPUTLENGTH 2048
#define MAXARGS 512

//global variables used for signal handling
int foregroundOnly = 0;
struct sigaction SIGINT_action = {0};
struct sigaction SIGTSTP_action = {0};

// struct that will carry all of the information parsed out from the input
struct inputCommand{
    char *inputArr[MAXARGS];    //array of just input commands
    char *readIn;               //redirection in file
    char *readOut;              //redirection out file
    int isBackground;           //is background process flag
    int argCount;               //number of arguments in inputArr
};

// struct that will contain backgroup pid's and number of processes
struct backgroundControl{
    pid_t bgPIDArr[100];
    int bgCount;
};

//Function prototypes
void catchSIGTSTP();
char * expandVariable(char *);
struct inputCommand * parseInputCreateCommand(char*);
struct inputCommand * getInput();
struct inputCommand * initIC();
void freeInputCommand (struct inputCommand *);
int executeOtherCommand(struct inputCommand *, struct backgroundControl *);
void changeDirectory(struct inputCommand *);
void printExitStatus(int);
void removeBgPID(struct backgroundControl *, int );
void killAllChildren(struct backgroundControl *);


/* catchSIGTSTP is the function that is run when the SIGTSTP signal is caught */
void catchSIGTSTP(){
    //if the program is not in forground only mode, we switch to this flag on
    if (foregroundOnly == 0){
        printf("\nEntering foreground-only mode (& is now ignored)\n: ");
		//write(1, "\nEntering foreground-only mode (& is now ignored)\n: ", 52);
		fflush(stdout);
		foregroundOnly = 1;
	}
    //othewise we turn the flag off
	else{
        printf("\nExiting foreground-only mode\n: ");
		//write(1, "\nExiting foreground-only mode\n: ", 32);
		fflush(stdout);
		foregroundOnly = 0;
	}

}

/* expandVariable takes a string passed to it and searches for all instances of "$$".
*    These character are then removed and the string is resized and in their place is
*    written the pid of program. This new string is then returned */
char * expandVariable(char *input){
    // max pid length: http://www.linfo.org/pid.html#:~:text=The%20default%20maximum%20value%20of,may%20require%20many%20more%20processes.
    char stringPID[6];
    sprintf(stringPID, "%d", getpid());

    //create dynamic copy of input on the heap
    char *inputCopy = malloc((strlen(input)+1)*sizeof(char));
    strcpy(inputCopy, input);

    // The following link gives idea of how to insert a string into a string by breaking
    //  the original string into two pieces and then concatenating them all in the correct order
    //https://stackoverflow.com/questions/2015901/inserting-char-string-into-another-char-string

    //find occurrences of expanded variable
    char *ptr = NULL;
    ptr = strstr(inputCopy, "$$");
    int counter;
    
    //if there are occurrences of the to be expanded variable continue looping through
    while (ptr != NULL){
        //counts through until we find the first instance of the eVar and match it to the location in our string
        for(int i = 0; i < (int)strlen(inputCopy); i++){
            if(&inputCopy[i] == ptr)
            {
                //we will use this counter for copying parts of the string later
                counter = i;
                break;
            }
        }

        //remove 2 char for $$ plus 1 for eol create temp string
        char *temp = (char*)calloc((strlen(inputCopy) - 2 + strlen(stringPID)+1),sizeof(char));
        //copy before $$ to the temp string using out counter
        strncpy(temp, inputCopy, counter);
        //concatenate the PID number on to the first half of the string
        strcat(temp, stringPID);
        //Copy the second half of the string just past $$ if it is not the end of file
        if(*(inputCopy + counter +2) != '\0'){
            strcat(temp, inputCopy + counter+2);
        }
        //realloc inputCopy's size to hold the longer temp string and copy temp to it
        inputCopy = realloc(inputCopy, strlen(temp)*sizeof(char)+1);
        strcpy(inputCopy, temp);
        //free temp
        free(temp);
        //check to see if there are any more instances of eVar
        ptr = strstr(inputCopy, "$$");
    }
    //copy our working input copy back to input and free the memory
    strcpy(input, inputCopy);
    free(inputCopy);

    return input;
}

/* initIC creates an new empty inputCommand struct */
struct inputCommand * initIC(){

    //allocate memory
    struct inputCommand *newIC = malloc(sizeof(struct inputCommand));
    //set all struct variables to NULL or 0
    newIC->readIn = NULL;
    newIC->readOut = NULL;
    newIC->isBackground = 0;
    newIC->argCount = 0;
    
    //set all arguments to null in the input array
    for(int i = 0; i < MAXARGS; i++){
        newIC->inputArr[i] = '\0';
    }
    return newIC;
}


/* the parseInputCreateCommand function takes a string and parses it, filling
*   up a inputCommand struct with the appropriate information. This inputCommand
*   struct is then returned */
struct inputCommand * parseInputCreateCommand(char* input){

    //Creates a new input command struct
    struct inputCommand *newIC = initIC();

    //Gets the first token from the string
    char *token = strtok(input," ");
    
    //Loop will run while there are still tokens in the string
    while (token){
        //If the toke is the input redirection character we do not save the character
        //  itself and get the next token which will be the redirection file name.
        //  Memory is allocated for the file name and then it is stored in the struct.
        if (!strcmp(token, "<")){
            token = strtok(NULL," ");
            newIC->readIn = calloc(strlen(token) + 1, sizeof(char));
            strcpy(newIC->readIn, token);
        }
        //Same as above, but for output redirection
        else if (!strcmp(token, ">")){
            token = strtok(NULL," ");
            newIC->readOut = calloc(strlen(token) + 1, sizeof(char));
            strcpy(newIC->readOut, token);
        }
        //If the token is not part of either redirection then it is part of the command
        //  and it can be saved to the input array
        else{
            newIC->inputArr[newIC->argCount] = calloc(strlen(token) + 1, sizeof(char));
            strcpy(newIC->inputArr[newIC->argCount], token);
            newIC->argCount++;
            
        }
        //continue searching for tokens
        token = strtok(NULL," ");
    }
    //token is freed
    free(token);

    //Set the isBackground flag if the background command is found. The spot in the array
    //  that was holding this command is then freed and set to NULL as it will not be
    //  passed as an argument
    if (strcmp(newIC->inputArr[newIC->argCount - 1], "&") == 0){
        free(newIC->inputArr[newIC->argCount - 1]);
        newIC->inputArr[newIC->argCount - 1] = '\0';
        newIC->isBackground = 1;
        newIC->argCount--;
    }

    //return the inputCommand struct for further use
    return newIC;
}

/* freeInputCommand frees the input command struct if it is not already null */
void freeInputCommand (struct inputCommand * inputC){
    //if the struct is null we do nothing, this is so we can catch empty commands
    //  and not have it adversely effect the program
    if(inputC == NULL){}
    //if it does contain arguments, free all that are present
    else{
        for(int i = 0; i < inputC->argCount; i++){
            free(inputC->inputArr[i]);
        }
        if(inputC->readIn != NULL){
            free(inputC->readIn);
        }
        if(inputC->readOut != NULL){
            free(inputC->readOut);
        }
        free(inputC);
    }
}

/* The getInput function gets the input from the user and passes this input to
*   the expandVariable function and then to the parseInputCreateCommand function
*   where it will then return the inputCommand struct with the appropriate information*/
struct inputCommand *getInput(){

    //getline set up
    size_t getLineBuf = 0;
    char *inputLine = NULL;

    printf(": ");
    fflush(stdout);
    getline(&inputLine,&getLineBuf,stdin);

    //The following link gives best practice advice on how to be standards correct
    //  for the next line of code (I was previously using strcpy instead of memmove
    //  and this was giving warnings in valgrind).
    //https://stackoverflow.com/questions/4823664/valgrind-warning-should-i-take-it-seriously

    //the new expanded input line is saved
    memmove(inputLine, expandVariable(inputLine), strlen(expandVariable(inputLine)));

    //remove newline at the end of the input, replace it with a null character
    //  this is because our string needs to be null terminated
    for(int i = 0; i < MAXINPUTLENGTH; i++){
        if(inputLine[i] == '\n'){
            inputLine[i] = '\0';
            break;
        }
    }

    //if a blank line entered we return null that will be handled in main()
    if(inputLine[0] == '\0'){
        return NULL;
    }

    //otherwise we pass the input line to be parsed and stored in an inputCommand struct
    struct inputCommand *newIC = parseInputCreateCommand(inputLine);
    free(inputLine);
    
    return newIC;
    
}

/* The changDirectory function is one of the built in functions that our shell uses. It
*   uses the arguments stored in the inputCommand struct to move around */
void changeDirectory(struct inputCommand *curInputCommand){

    //only 'cd' no other arguments we move to the home directory
    if(curInputCommand->argCount == 1){
        chdir(getenv("HOME"));
    }
    //The following link gives good examples of how to use perror with chdir
    //https://www.geeksforgeeks.org/chdir-in-c-language-with-examples/

    //look at next argument for the directory or command specified.
    else{
        if(chdir(curInputCommand->inputArr[1]) != 0){
            perror("Error, no such directory");
        }
    }

}

/* The executeOtherCommand function forks the parent process and runs the command arguments
*   that are stored in the inputCommand array via execvp. These processes can be run in
*   the background or foreground depending on the conditions. This function also sets
*   input and output redirection using dup2() if any redirection is passed. The child
*   processes are also monitored and stored via a backgroundControl struct. The exit status
*   of the last process completed is then returned. */
int executeOtherCommand(struct inputCommand *curInputCommand, struct backgroundControl *bgCtrl){
    
    //most of the code below was adapted from the following lecture explorations and repl.it
    //Exploration: Process API - Monitoring Child Processes
    //Exploration: Process API - Executing a New Program
    int   exitStatus, input, output, result;
	
    //fork our process creating a child process with a spawnPid = 0
    pid_t spawnPid = fork();

    switch(spawnPid){
        
        //-1 is only returned if there is an error
        case -1:
            perror("fork() failed!");
            exit(1);
            break;

        // the fork returns a 0 for the child process which the switch statement will run here
        case 0:
            // Child process executes this branch
            //most of this section was modified from Exploration: Processes and I/O

            //if readIn is not null that means there is a redirection command
            if(curInputCommand->readIn){

                //open the file in read only
                input = open(curInputCommand->readIn, O_RDONLY);
                if (input == -1) {
                        perror("Unable to open input file\n");
                        exit(1);
                }
                //use dup2 to set our input from stdin to the input redirection
                result = dup2(input, 0);
                if (result == -1) {
                        perror("Unable to assign file\n");
                        exit(2);
                }
                // close file descriptor on exec from Exploration: Processes and I/O
                fcntl(input, F_SETFD, FD_CLOEXEC);
            }

            //if this is a background process and read in redirection has not been specified
            //  we are told in the assignment outline that we should still redirect to /dev/null.
            //  So below is exactly the same as above, but always directs to /dev/null.
            if(!curInputCommand->readIn && curInputCommand->isBackground) {

                input = open("/dev/null", O_RDONLY);
                if (input == -1) {
                        perror("Unable to open /dev/null\n");
                        exit(1);
                }
                result = dup2(input, 0);
                if (result == -1) {
                        perror("Unable to assign /dev/null\n");
                        exit(2);
                }
                fcntl(input, F_SETFD, FD_CLOEXEC);
            }

            //below here is the same as above but for read out. This includes the section
            //  for if the process is a background process and output redirection has not
            //  been set.
            if(curInputCommand->readOut){

                //open write only, create if it doesn't exist and truncate if it does
                output = open(curInputCommand->readOut, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (output == -1) {
                        perror("Unable to open output file\n");
                        exit(1);
                }
                result = dup2(output, 1);
                if (result == -1) {
                        perror("Unable to assign output file\n");
                        exit(2);
                }
                fcntl(output, F_SETFD, FD_CLOEXEC);
            }
            if(!curInputCommand->readOut && curInputCommand->isBackground){

                 output = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (output == -1) {
                        perror("Unable to open /dev/null\n");
                        exit(1);
                }
                result = dup2(output, 1);
                if (result == -1) {
                        perror("Unable to assign /dev/null/\n");
                        exit(2);
                }
                fcntl(output, F_SETFD, FD_CLOEXEC);
            }

            //if this is not a background command (ie forground) the child process must terminate
            //  when it receives the SIGINT signal, since we previously set the SIGINT signal to 
            //  be ignored in main we restore the signal handler to SIG_DFL for this child process
            if(!curInputCommand->isBackground){
                SIGINT_action.sa_handler = SIG_DFL;
			    SIGINT_action.sa_flags = 0;
			    sigaction(SIGINT, &SIGINT_action, NULL);
            }
            
            //This executes the command via execvp
            if(execvp(curInputCommand->inputArr[0], curInputCommand->inputArr)){
                
                //This only appears if there is an error
                //  the command argument that was given is passed as error text and then freed
                char *errorTxt = malloc((strlen(curInputCommand->inputArr[0])+1)*sizeof(char));
                sprintf(errorTxt, "%s", curInputCommand->inputArr[0]);
                perror(errorTxt);
                freeInputCommand(curInputCommand);
                exit(2);
            }
            break;

        //the parent process will run here
        default:

            //if the child process is a background process we print the child process id to screen
            //  the child proces ID is then stored in background process ID array so that it can
            //  be monitored
            if(curInputCommand->isBackground){

                //WNOHANG doesn't wait for the child to finish compeleting 
                waitpid(spawnPid, &exitStatus, WNOHANG);
				printf("Background pid is %d\n", spawnPid);
				fflush(stdout);
                //stores process ID and increases background process count
                bgCtrl->bgPIDArr[bgCtrl->bgCount] = spawnPid;
                bgCtrl->bgCount++;
            }
            //if the child proccess not a background process we then wait for it to complete
            else{
                
                waitpid(spawnPid, &exitStatus, 0);
            }
            //here we check to see if any child process has completed. The -1 value passed to waitpid
            //  mean that it will look for any child process, the WNOHANG means that that waitpid will
            //  return the pid of the child that has completed or 0 if they have not changed state. For
            //  for this we use the while loop to keep checking until there are no more processes that have
            //  completed
            while ((spawnPid = waitpid(-1, &exitStatus, WNOHANG)) > 0) {
                //if a process has compelted we remove it from being monitored in our background pid array
                removeBgPID(bgCtrl, spawnPid);
                //the process id and exit status are then printed to screen
			    printf("Background PID %d is done: ", spawnPid);
			    printExitStatus(exitStatus);
			    fflush(stdout);
                //check to see if there are more processes that have finished
                spawnPid = waitpid(-1, &exitStatus, WNOHANG);
		    }
    }
    //the last exit status is then returned
    return exitStatus;
}

/* removeBgPID removes a background process from monitoring via the background
*   process ID array. */
void removeBgPID(struct backgroundControl *bgCtrl, int pid){
    //loop though the bgPID array and compare process IDs
    for(int i = 0; i < bgCtrl->bgCount; i++){
        //if the process if found it is overwritten with the last element in the array
        //  and the loop is exited. Replacing with the last element in the array allows
        //  us to not have to loop through all locations in the array shifting them by 1
        if(bgCtrl->bgPIDArr[i] == pid){
            bgCtrl->bgPIDArr[i] = bgCtrl->bgPIDArr[bgCtrl->bgCount - 1];
            break;
        }
    }
    //decrease background count
    bgCtrl->bgCount--;
}


/* killAllChildren function takes the background control struct and sends a SIGQUIT 
*   kill signal to stop all of them. This is used during exit or if you are Anakin Skywalker
*   and have some beef with younglings  */
void killAllChildren(struct backgroundControl *bgCtrl){
    for(int i = 0; i < bgCtrl->bgCount; i++){
        kill(bgCtrl->bgPIDArr[i], SIGQUIT);
    }
    bgCtrl->bgCount = 0;
}


/* printExitStatus takes an exit status and then prints to screen the exit value or termination
*   signal depending on how it was exited */
void printExitStatus(int exitStatus){
    //if the process exited
    if (WIFEXITED(exitStatus)) {
		printf("exit value %d\n", WEXITSTATUS(exitStatus));
        fflush(stdout);
	}
    //else the process was terminated
    else {
		printf("terminated by signal %d\n", WTERMSIG(exitStatus));
        fflush(stdout);
	}
}

/* this is the main function that drives the program. The signal handlers are also set here 
*   as well as the loop for continuing to get information from the user*/
int main(){

    //initialize our background control struct that we will use to monitor background processes
    struct backgroundControl *bgCtrl = malloc(sizeof(struct backgroundControl));
    memset(bgCtrl->bgPIDArr, 0, 100);
    bgCtrl->bgCount = 0;

    int cont = 1;
    pid_t exitStatus;
    struct inputCommand *curInputCommand = NULL;

    //The following signal handler lines were derived from the code in the lectures
    //  in the Exploration: Signal Handling API section and Exploration: Signals –
    //  Concepts and Types

    //catch SIGINT - interupt
	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

    //catch SIGTSTP - terminal stop and redirect it to the catchSIGTSTP function
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
    //the SA_RESTART flag allow automatic restart of the call the signal interupted
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    

    
    do{
        //get the input command struct that contains all the command data
        curInputCommand = getInput();

        //nothing entered or a comment
        if((curInputCommand == NULL) || *(curInputCommand->inputArr[0]) == '#' ){
            freeInputCommand(curInputCommand);
            printf("\n");
            fflush(stdout);
            continue;
        }
        //built in Exit command
        else if(strcmp(curInputCommand->inputArr[0], "exit") == 0){
            printf("Exiting...\n");
            fflush(stdout);
            killAllChildren(bgCtrl);
            cont = 0;
        }
        //built in Change Directory command
        else if(strcmp(curInputCommand->inputArr[0], "cd") == 0){
            changeDirectory(curInputCommand);
        }
        //built in Status command: The status command prints out either the exit status or the 
        //  terminating signal of the last foreground process ran by your shell
        else if (strcmp(curInputCommand->inputArr[0], "status") == 0){
            printExitStatus(exitStatus);
        }
       
        //All other commands are passed to executeOtherCommand
        else{
            //if foreground only mode is enabled, reset the background flag
            if(foregroundOnly){
                curInputCommand->isBackground = 0;
            }
            exitStatus = executeOtherCommand(curInputCommand, bgCtrl);
            
        }

        //resets and clears arrays
        freeInputCommand(curInputCommand);

    }while(cont); 
    
    free(bgCtrl);

    return 0;
}



