#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "opsys.h"


/**************************************************************************
*                                                                         *
*                       CSE 333 Operatin Systems                          * 
*                                                                         *
*                       Programming Assignment 2                          *
*                                                                         *
*                             Özer Baday                                  *
*                                                                         *
***************************************************************************/


#define MAX_CHAR 1024

#define OPEN_FLAGS_READ (O_RDONLY) // Use these flags when reading from file <
#define OPEN_FLAGS_WRITE (O_WRONLY | O_TRUNC | O_CREAT) //Use these flags when writing to a file >
#define OPEN_FLAGS_APPEND (O_WRONLY | O_CREAT | O_APPEND) //Use these flags when appending to a file >>
#define CREATE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) //Modes to be used when opening a file

#define FOREGROUND_FLAG 10 // Use if process is foreground 
#define BACKGROUND_FLAG 11 // Use if process is background

#define PATH_ADD    0 // To pass path_change function when adding a path
#define PATH_REMOVE 1 // To pass path_change function when removing a path

extern char **environ; // The variable environ points to an array of pointers to strings called the "environment". (See manual page environ (5))

int process_flag; // Whether the current process is foreground

char **argpipe; //Pipelined arguments

char recent_commands[11][MAX_CHAR]; // String array that holds command history to be accessed by pressing <Ctrl> <C>

int back_process_count; // Number of background processes




int file_open(char * filename, int flags, int create_mode, int fileno) // Function to adjust file descriptor tables by calling from another function
{                                                                   
    int fd;
    fd = open(filename, flags, create_mode); // Try to open file with given flags and parameters and assign it to fd
    if(fd == -1) // If that operation failes
    {
        perror("Failed to open file"); // Print error message
        return -1;                     // Return unseccessfully
    }
    if (dup2(fd, fileno) == -1)        // Try to replace given fileno with recently opened file
    {
        perror("Failed to redirect standard input"); // If failes print error message
        return -1;                                   // Return unsuccessfully
    }
    if (close(fd) == -1)                             // Try to close necessary duplication of fd from file descriptor table 
    {
        perror("Failed to close the file");          // If failes print error message
        return -1;                                   // Return unsuccessfully
    } 
}

int redirect(char *io_part) // Function that handles io redirection by splitting io part of input line into tokens
{
    char **io_parts; // String array to hold each tokens such as '>' and filenames
    char delim[] = " \t\n"; // delimiters assuming there are whitespaces between every element
    int count; // Number of tokens after splitting io string
    

    if ((count = makeargv(io_part, delim, &io_parts)) == -1) // Split string into string array with given delimiters
    {
        fprintf(stderr, "Failed to construct an argument array for %s\n", io_part); // If failes print an error message
        return -1; // Return unsuccessfully
    }

    int i; // int variable to be used in for loop

    for(i = 0; i < count; i++ ) // For loop to visit each element of string array which holds io operators and filenames
    {
        if(strcmp(io_parts[i], "<" ) == 0) // If '<'' operator found in an element
        {
            file_open(io_parts[i+1], OPEN_FLAGS_READ, CREATE_MODE, STDIN_FILENO); // then the next element is filename and arrage file descriptor table  
            i++;                                                                  // by redirecting standard INPUT to that file
        }
        else if(strcmp(io_parts[i], ">" ) == 0)
        {
            file_open(io_parts[i+1], OPEN_FLAGS_WRITE, CREATE_MODE, STDOUT_FILENO); // then the next element is filename and arrage file descriptor table
            i++;                                                                    // by redirecting standard OUTPUT to that file
        }
        else if(strcmp(io_parts[i], ">>" ) == 0)
        {
            file_open(io_parts[i+1], OPEN_FLAGS_APPEND, CREATE_MODE, STDOUT_FILENO); // then the next element is filename and arrage file descriptor table
            i++;                                                                      // by redirecting standard OUTPUT to be appended to that file
        }            
    }
    return 0; // Return successfully
}


//Function that checks a given command whether it is in PATH and executable.
//And returns the order of the directory in argp.
int path_search(char * command, char **path_return) 
{
    char **argp; // String array to pass each path in PATH variable
    int i;       // To be used in for loop
    int count;   // Number of paths in PATH variable
    char delim[] = ":"; // delimeter is ':' beacuse paths are split using semicolons
    char *path = getenv("PATH"); // Get current PATH variable

    if((count = makeargv( path , delim , &argp )) == -1) // Try to split PATH using delimeter : and pass it to string array argp
    {
        perror("Unable to make argv\n"); // If failes print error message
        return -1;                      // Return unsuccessfully
    }

    for(i = 0 ; i < count ; i++) // To visit each token of string array which contains paths
    {
        char control[MAX_CHAR] = ""; // String to get path and concanate command with slash i.e. /ls
        strcat(control, argp[i]); // concanate current path into control string
        strcat(control, "/");     // concanate /
        strcat(control, command); // concanate the given command at the end of the string
        if(access(control, X_OK) == 0) // If the file in this path and executable 
        {
            strcpy(*path_return, control); // Copy it to path_return string to be used in exec functions
            return i;                      // Return the order of this path in PATH variable
        }
    }
    return -1; // If function couldn't return until here then it failed to find. Return -1 Unsuccessfully
}

int command_dir_match(char * command, char * dir) // To determine whether given directory matches with the given command
{
    char match[MAX_CHAR]; // String to hold string in dir that matches with command
    if(strstr(dir,command) != NULL) // If there is a match
    {
        strcpy(match, strstr(dir, command)); // Copy it to match variable
        if(strcmp(match, command) == 0) // And then if that match is command itself
        { 
            return 0; // Return 0 as indication of success
        }
        else // If not 
        { 
            return -1; // Return -1 as indication of failure
        } 
    }  
    return -1;  // Means there is no match.  Return unsuccessfully
       
}




int directory_detection(int numtokens,char **args) // to determine whether a directory specified as last parameter for execl control
{
    if(strncmp(args[numtokens -1], "/", 1) == 0 ) // directory specification found
        return numtokens - 1;   // return the order of token which includes directory   
    else 
        return -1;
}

int io_detection(char *input_line) // detection of io redirection in input string
{
    int i;
    for(i = 0; i < strlen(input_line); i++) // To be able to detect wanted characters visit each char of input_line string
        if(input_line[i] == '<' || input_line[i] == '>' || input_line[i] == '&') // If there is one of the io redirection characters 
            return i; // Return the order of first encounter with the char. Ampersand character is included because it should be cut out even if there is no io redirection 
    return -1; // If no ampersand or io redirection then return unsuccessfully
}

int background_detection(char *input_line) // Detection of & character which indicates run in background
{
    int length = strlen(input_line); // The length of the input_line to be able to determine the existence of '&' character
    if(input_line[ length - 2 ] == '&' ^  input_line[ length - 1] == '&') // If & is the last char or the one before it (maybe a space after that) but not both 
        process_flag = BACKGROUND_FLAG; // Set global process_flag as BACKGROUND as an indication of next child will be running in background
    else
        process_flag = FOREGROUND_FLAG; // If conditions of background process above are not true then it is a foreground process
}

char * io_parser(char *input_line, int i) // Cuts the io part of input string and passes it to another string (i represents the start of io redirection part in input line)
{

    int length = strlen(input_line); // Take the length of the input_line

    char * temp = (char *) malloc(MAX_CHAR);  // Temp string to hold input_line temporarily
    char * io_part = (char *) malloc(MAX_CHAR); // To hold io part of the input_line and to be returned

    strcpy(temp, input_line); // Copy input_line to temp
    strncpy(io_part, input_line + i, length - i); // Copy input_line to io_part starting from the io part to the end of the string even including &
    memset(input_line, 0, length); // Set every element of input line as 0
    strncpy(input_line, temp, i); // initialize input_line with the ignoring "cut" part. So io and ampersand is cut out

    return io_part; // Return io_part to another function to handle io redirection
}

char * show_cwd() // Shows current working directory
{
    char * cwd = (char *) malloc(MAX_CHAR); // Empty string to assign current working directory
    if (getcwd(cwd, sizeof(char) * MAX_CHAR) != NULL) // Try to get the current working directory with getcwd function 
    {
        return cwd; // In case of success return it
    }
    else // If there is a failure
        perror("getcwd() error");  // Print an error message
}

int set_shell() // Set shell as the absolute path of this program.
{
    char shell[MAX_CHAR] = ""; // String to be set as new SHELL variable
    strcat(shell, getenv("PWD")); // Concanate current working directory
    strcat(shell, "/333sh"); // Add the name of the program
    setenv("SHELL", shell, 1); // Set string to be the new SHELL variable
    return 0;
}

int printenv() // Print all environment variables
{
    char **env; 
    for (env = environ; *env; ++env) 
        printf("%s\n", *env);
}

int handle_set(char **argset, int numtokens)
{
    char variable[MAX_CHAR];
    char value[MAX_CHAR];


    if(numtokens == 2) // Means no whitespaces around the assignment operator '='
    {
        int i;
        int length = strlen(argset[1]); // length of string given to set as argument 
        for(i = 0; i < length; i++)
        {
            if(argset[1][i] == '=')
            {
                strncpy(variable, argset[1], i);
                strncpy(value, argset[1] + i + 1, length - i - 1);
                setenv(variable, value, 1);
                return 0;
            }
        }
        fprintf(stderr, "%s\n", "Something wrong with the argument");
        return 1;
    }
    else if(numtokens == 4) // Means there are whitespaces around the assignment operator '='
    {
        if( strcmp (argset[2], "=") == 0)
        {
            strcpy(variable, argset[1]);
            strcpy(value, argset[3]);
            setenv(variable, value, 1);
            return 0;
        }
        else
            fprintf(stderr, "%s\n", "Illegal usage of set arguments");
    }
}

int path_change(int operation_flag, char *given_path)
{
    char **argp; 
    int i;
    int count;
    char delim[] = ":";
    char *path = getenv("PATH");
    char new_path[MAX_CHAR] = "";  // Inıtialization of string that is going to be new PATH

    if(operation_flag == PATH_ADD) // If operation is adding to PATH
    {
        strcat(path, ":"); // Add a new semicolon to the current PATH
        strcat(path, given_path); // Add given directory to the current PATH
        setenv("PATH", path, 1); // Set resulted string as environment variable PATH
        return 0; 
    }
    else // If operation is removing from PATH
    {
        if((count = makeargv( path , delim , &argp )) == -1) // Split PATH that delimited by semicolons into a string array
        {
            perror("Unable to makeargv the PATH variable\n");
            return -1;
        }

        for(i = 0; i < count; i++) 
        {
            if(strcmp(argp[i], given_path) != 0) //If directory wanted to be removed does not match with ith element of array 
            {
                strcat(new_path, argp[i]); // add it to new_path string which is going to be new PATH

                strcat(new_path, ":"); // add a semicolon at the end of the string 
            }
        }

        int length = strlen(new_path);
        if(new_path[length -1] == ':') // To clear ':' at the end of the string that will be the new path 
            new_path[length -1] = '\0';
        
        setenv("PATH", new_path, 1); // Set the string, which is created by removing given direcory match(es) from PATH, as the new PATH  

        return 0;
    }

    

}

void place_recent_command(char* input_line) // Places entered input to recent_command string array to be able to see when pressing Ctrl + C
{
    
    int i;
    for(i = 9; i >= 0; i--) // Copy 9.th element to 10th, 8th to 9th and so on this way keep recent 10 commands
    {
        if(strcmp(recent_commands[i - 1], "") != 0) // if (i-1)th cell is not empty
            strcpy(recent_commands[i], recent_commands[i -1]); // copy it to ith cell
    }

    strcpy(recent_commands[0], input_line); // Place most recent command to 1st cell of the recent_commands array 
}

void print_recent_commands() // Print most recent 10 commands with for loop
{
    printf("\n");
    int i;
    for(i = 0; i < 10; i++)
    {
        if(strcmp(recent_commands[i], "") != 0) // If not empty
            printf("%d. %s", i, recent_commands[i]); // Print it
    }
}




void catchctrlc(int signo) // Call this function when <Ctrl> <C> signal catch
{
    print_recent_commands(); // Print recent commands
    printf("333sh: "); // Print the prompt beacuse it skipped 
    fflush(stdout); // Flush standard output
    signal(SIGINT, catchctrlc); // 
}

void catch_child(int signo) // Call this function when a process termination signal catched
{
    int pid;
    pid = waitpid(-1, NULL, WNOHANG); // Return status information of terminated process

    if(pid > 0) // If terminated process is a child process
        back_process_count--; // Decrease background process count by one 
    
    signal(SIGCHLD, catch_child); //
}

int setup()
{

    int   p[2];
    pid_t pid;
    int   fd_in = 0;


    pid_t childpid;

    int dir_in_args; // order of the directory in args
    char * path = (char *) malloc(MAX_CHAR);
    char * io_part = (char *) malloc(MAX_CHAR);  

    int i, j;
    int num_of_coms; //number of commands that piped
    int io_cut;

    char delim_pipe[] = "|\n";
    char delim_command[] = " \t\n";

    char input_line[ MAX_CHAR ];
    fgets(input_line, MAX_CHAR , stdin);

    if(strcmp(input_line, "\n") == 0)
    {
        return 0;   
    }

    // Recent command check before splitting input string 

    if(strlen(input_line) == 3 && input_line[0] == '!') // If '!' entered it means that one of the recent commands will be executed
    {
        if(input_line[1] >= '0' && input_line[1] <= '9') // If the character after ! is [0-9] 
        {
            if(strcmp(recent_commands[ input_line[1] - 48 ], "") != 0) // Take the numerical value of the character to control whether that cell is empty
            {
                int order = input_line[1] - 48; // pass numeroical value to order
                strcpy(input_line, recent_commands[order]); // Initialize current input as that order of recent_command
                printf("%s", input_line); // Print that input_line
            }
            else // If given number of cell is empty
                return -1; // Return unsuccessfully

        }
        else if(input_line[1] == '!') // If second character is ! means most recent command
        {
            if(strcmp(recent_commands[0], "") != 0) // If first cell of recent_commands is not empty 
            {
                strcpy(input_line, recent_commands[0]); // Initialize current input_line with the most recent command 
                printf("%s", input_line);  // Print that input_line
            }
            else // If most recent command is not initialized yet 
                return -1; // Return unsuccessfully
            
        }
        else 
        {
            fprintf(stderr, "%s\n", "Something wrong with the command. Usage!! or !n  n is number of command [0,9] "); // If no number or ! entered next to !
            return -1; // Return unsuccessfully
        }
    }

    place_recent_command(input_line); // Place current input_line as the most recent command by calling the corresponding function

    if ((num_of_coms = makeargv(input_line, delim_pipe, &argpipe)) == -1) // Split input_line delimited by pipes into argpipe string array
    {
        fprintf(stderr, "Failed to construct an argument array for %s\n", input_line); // Print error message in case of failure
        return -1;
    }

     

    char **args[num_of_coms];
    int numtokens[num_of_coms];

    for(i = 0; i < num_of_coms; i++)
    {
        background_detection(argpipe[i]); // Detect ampersand symbol in particular command

        int io_cut; // Variable holds the value of where to cut the string

        if( (io_cut = io_detection(argpipe[i])) != -1 ) // If there is a io redirection or ampersand in string assign the place of first encounter
            strcpy(io_part, io_parser(argpipe[i], io_cut)); // Cut the string starting from that place into io_part
    

        if ((numtokens[i] = makeargv(argpipe[i], delim_command, &args[i])) == -1) // Split command string into args string array arrays first string array to pass exec function
        {
            fprintf(stderr, "Failed to construct an argument array for %s\n", input_line); // Print error message in case of failure
            return -1; // Return unsuccessfully
        }




        /****************************************************
        *                                                   *
        *                                                   *
        *    BUILD-IN COMMANDS WILL BE IMPLEMENTED BELOW    *
        *                                                   *
        *                                                   *
        *****************************************************/




        if(strcmp(args[i][0], "exit") == 0) // If user entered exit as command
        {
            if(back_process_count > 0) // If there are background processes
                fprintf(stderr, "Unable to exit.There are %d background processes running.",  back_process_count); // Don't allow user to exit.And print an error message.
            else
                exit(0); 
        }
            


        else if(strcmp(args[i][0], "cd") == 0) // If the first argument is cd, change directory.
        {
            if(numtokens[i] == 1) // If no argument is present show current working directory
            {
                printf("%s\n", getenv("PWD")); // Call the function that shows current working directory
            }
            else if(chdir(args[i][1]) == 0) //If changing directory is successful 
            {
                setenv("PWD", show_cwd(), 1); //then change the PWD environment variable 
            }
            else //In case of an error when changing the directory
            {
                perror("Unable to change directory"); // Print the error message
                return -1;
            }
        }
            

        else if(strcmp(args[i][0], "clr") == 0) // If the entered command is clr
        {
            system("clear"); //then clear the screen
        }

        else if(strcmp(args[i][0], "print") == 0) // Of the entered command is print
        {
            char *env = (char *)malloc(MAX_CHAR);

            if(numtokens[i] == 1) // If no argument is present
            {
                //List the environment settings
                printenv();
            }
            else if((env = getenv(args[i][1])) != NULL) // If getenv finds variable and returns it successfully 
            {
                printf("%s\n", env); // then print it.
            }
            else // In case of environment variable couldn't be found 
            {
                perror("Unable to find environment"); // print the error message
                return -1;
            }
        }

        else if(strcmp(args[i][0], "set") == 0) // If entered command is set
        {
            if(numtokens[i] < 2 || numtokens[i] > 4) // If correct number of arguments are not given. 2 for without and 4 for with whitespaces around '='
            {
                fprintf(stderr, "%s\n", "Undefined arguments for set ! Usage: set variable = value (with or without whitespaces)"); // Print error message
            }
            else // If number of arguments are not wrong
            {
                handle_set(args[i], numtokens[i]); // Handle set command by calling handle_set function
            }
            
        }

        else if(strcmp(args[i][0], "where") == 0) // If entered command is where
        { 
            if(numtokens[i] == 2) // Usage where <command> so there must be 2 arguments
            {
                char *path_to_command = (char *) malloc (MAX_CHAR); 
                if(path_search(args[i][1], &path_to_command) != -1) // Try to find the command in path
                {
                    printf("%s\n", path_to_command); // If successful print the path
                    return 0;
                }
                else
                    fprintf(stderr, "%s\n", "Unable to locate command");

            }
            else
                fprintf(stderr, "%s\n", "Something is wrong with the arguments. Usage: where <command> "); 
            
        }

        else if(strcmp(args[i][0], "path") == 0) // If entered command is path
        {
            if(numtokens[i] == 1) // And there is no argument for path command
            {
                printf("%s\n", getenv("PATH")); // Print the current PATH
                return 0; 
            }
            else if(numtokens[i] == 3) // If there are 2 arguments for path command
            {
                if(strcmp(args[i][1], "+") == 0) // And the first argument is '+'
                    path_change(PATH_ADD, args[i][2]); // Call path_change with PATH_ADD flags and give the 2nd argument as it is

                else if(strcmp(args[i][1], "-") == 0) // And the first argument is '-'
                    path_change(PATH_REMOVE, args[i][2]); // Call path_change with PATH_REMOVE flags and give the 2nd argument as it is

                else
                    fprintf(stderr, "%s\n", "Something is wrong with the arguments. Usage: path <+ or -> <path> (arguments are optional)");
            }
            else
                fprintf(stderr, "%s\n", "Something is wrong with the arguments. Usage: path <+ or -> <path> (arguments are optional)");
        }



        
        /*******************************************************
        *                                                      *
        *                                                      *
        *     EXTERNAL COMMANDS WILL BE IMPLEMENTED BELOW      *
        *                                                      *
        *                                                      *
        ********************************************************/
        
    


    
        else //Not an internal command then
        {
            pipe(p);

            if ((childpid = fork()) == -1) 
            {
                exit(EXIT_FAILURE);
            }
            if (childpid == 0) 
            {  
                dup2(fd_in, 0); // Change the input according to the old one

                if(i + 1 < num_of_coms)
                    dup2(p[1], 1);
                close(p[0]);

                redirect(io_part);
            


                if( ( dir_in_args = directory_detection(numtokens[i],args[i]) ) != -1 && //if a directory specified in input_line, initialize the order in arguments to dir_in_args
                    
                    command_dir_match( args[i][0], args[i][dir_in_args] ) == 0 && //if given command matches with the filename in given directory
                    
                    path_search( args[i][0] , &path )  != -1 && // find path of the command in PATH variable
                    
                    strcmp( path, args[i][dir_in_args]) == 0) // if given directory matches with the actual path of command
                {
                /**************************************************
                *    EXECL PART                                   *
                **************************************************/

                    args[i][dir_in_args] = NULL;

                    execl( path, args[i][0], args[i][1], args[i][2], args[i][3], args[i][4], args[i][5], args[i][6], args[i][7], args[i][8], args[i][9]
                    , args[i][10], args[i][11], args[i][12], args[i][13], args[i][14], args[i][15], args[i][16], args[i][17], args[i][18], args[i][19]
                    , args[i][20], args[i][21], args[i][22], args[i][23], args[i][24], args[i][25], args[i][26], args[i][27], args[i][28], args[i][29], NULL);  

                    perror("Child failed to execls the command");
                    return 0;   

                }
                else if(path_search(args[i][0], &path) != -1) // If path of command is found in PATH variable
                {
                /**************************************************
                *    EXECV PART                                   *
                **************************************************/
                    execv(path, &args[i][0]); // Execute with the given path
                    perror("Child failed to execvp the command");
                    return 0;

                }
                else   // If conditions are not satisfied print error message and exit from child process with failure.
                {
                    fprintf(stderr, "%s\n", "No exec accepted the command");
                    exit(EXIT_FAILURE);
                }    
        
            }
            else if(process_flag == BACKGROUND_FLAG) //parent code
            {
                back_process_count ++; // If current process is background process increase the background Process counter by one
                close(p[1]);
                fd_in = p[0];
            }
            else //parent code
            {
                wait(NULL);
                close(p[1]);
                fd_in = p[0]; // Save the input for the next command
            }
        } 
    } 
}

int main(int argc, char *argv[])
{

    signal(SIGINT, catchctrlc); 
    signal(SIGCHLD, catch_child); 

    back_process_count = 0;

    set_shell(); // set SHELL variable initially to be able to see the variable all around the program.
    
    while(1)
    {
        printf("333sh: ");
        setup();    
    }
    
}

