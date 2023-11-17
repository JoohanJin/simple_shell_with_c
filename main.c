/******************************************************

* Development platform: WSL2 Ubuntu 22.04
* Remark: All the part has been done.
* Compilation: make

******************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>

#define MAX_COMMANDS 5 // maximum five pipelined commands
#define MAX_COMMAND_LENGTH 1024 // 1024 characters as an upper bound
#define MAX_STRING 30 // 30 strings in the command

// Global Variable
pid_t child_pid[MAX_COMMANDS];
int child_ready;
char run_stat[MAX_COMMANDS][MAX_COMMAND_LENGTH];
volatile sig_atomic_t stop = 0;
sigjmp_buf jmpbuf;
int command_counter = 0;


void executeCommand(char *);
void pipedCommandLine(char *);
void runningStat(int, int, int);

// to trim the white space around the command when tokenizing commands with the delimiter "|" for pipe
char *trimWhiteSpace(char *command) {
    int a = strlen(command) - 1;
    int b = 0;
    while (isspace(command[a]) && a >= 0){
        a--;
    }
    while (isspace(command[b]) && b <= a){
        b++;
    }
    int i = 0;
    while (b <= a){
        command[i++] = command[b++];
    }
    command[i] = '\0';
    return command;
}

// to trim the command itself when the program is getting the command. --> if there is only a white space just skip this iteration and goes to the next loop 
int is_all_whitespace(const char *str) {
    while (*str) {
        if (!isspace((unsigned char)*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

// SIGUSR1 handler for the shell to control the child process to be executed
void sigusr1_handler(int signum){
    child_ready = 1; // let' use flag!
}

// SIGINT handler: when the shell catch the SIGINT (ctrl-c) then it just goes back to the beginning of the loop.
void sigintHandler(int signum) {
   siglongjmp(jmpbuf, 1);

}

/*
main() function
the infinite loop in the function lets the user to input the command into the buffer
Commands stored in the buffer will be preprocessed for the easy use in the exeuction process.
JCshell will execute the command in the buffer at the final stage.
*/
int main(){
    // register signal handler for SIGINT in core process
    struct sigaction sa;
    sa.sa_handler = sigintHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // sigaction for SIGUSR1
    struct sigaction sa_user1;
	sa_user1.sa_flags = SA_RESTART;
	sa_user1.sa_handler = &sigusr1_handler;
	sigaction(SIGUSR1, &sa_user1, NULL);

    // command buffer, char array, with the maximum length 1024, will be used to store the command from the user.
    char command[MAX_COMMAND_LENGTH];

    while (1){ // infinite loop for the shell
        if (sigsetjmp(jmpbuf, 1) == 1) {
            printf("\n"); // Print a new line
        }
        // Display the command prompt, which indicates it is waiting for the user's input.
        printf("## JCshell [%d] ##  ", getpid()); // with the functionality displaying the process id of the shell
        fgets(command, MAX_COMMAND_LENGTH, stdin); // get the input from the user
        command[strcspn(command, "\n")] = '\0'; // remove the new line symbol, '\n' at the end of command

        // if command consists of only white space, then it just skips execute it
        if (is_all_whitespace(command)){
            continue;
        }
        
        // keyword "exit" requirements
        if (strncmp(command, "exit", 4) == 0) { // need to implement exit + argument error
            int indx = 4;
            if (command[indx] == ' '){
                while (command[indx++] == ' ');
            }
            if (command[indx] == '\0'){
                printf("JCshell: Terminated\n");
                exit(0);
            }
            else {
                printf("jCshell: \"exit\" with other arguments!!!\n");
                continue;
            }
        }

        else {
            pipedCommandLine(command);
        }
    }
}

/*
pipedCommandLine() function will do the following tasks:
- it firstly parse the command into the array format so that it can be passed as parameters to exec..() function
- implement the pipeline within the shell upto 5 concatenated commands.
- error handling functions
    e.g. more than 5 commands, spcae in between two pipes and so on.
*/
void pipedCommandLine(char *command){
    // check if SIGINT has been blocked
    int blocked = 0;
    // string array to get the command separated with delimeter "|"
    char *piped_commands[MAX_COMMANDS];
    sigset_t blkset;
    int pipe_counter = 0;
    command_counter =0; 
    int i;

    // counter the number of "|", as pipecounter which should be eqaul to (the number of command - 1)
    for (i = 0; i < strlen(command); i++){
        if (command[i] == '|'){
            pipe_counter++;
        }
    }

    // tokenize the command with the delimeter "|"
    char *commands = strtok(command, "|");
    while (commands != NULL){
        if (command_counter == 5){
            command_counter++;
            break;
        }
        // if there is a white space in between "|", then prints the error message and going back to the main loop
        if (is_all_whitespace(commands)){
            printf("JCshell: should not have two | symbols without in-between command\n");
            return;
        }
        piped_commands[command_counter++] = commands;
        commands = strtok(NULL, "|");
    }

    // if more than five commands are connected with pipe, it raises the error and going back to the main loop
    if (command_counter > 5){
        printf("the number of commands is more than a limit.\nNow we are waiting for new command!\n");
        return ;
    }

    // Since pipe_counter always should be (command_counter - 1), it should raises the error
    // if there are more or eqaul number of pipe than command.
    if (pipe_counter >= command_counter){
        printf("JCshell: should not have two | symbols without in-between command\n");
        return;
    }

    // initialize the pipe
    int fd[pipe_counter][2];
    // for SIGUSR1
    child_ready = 0;

    // activate the pipeline for the command
    for (int j = 0; j < pipe_counter; j++){
            pipe(fd[j]);
    }
        
    for (i = 0; i < command_counter; i++){
        pid_t pid = fork();

        if (pid < 0){ // fork error
            // if there is an error for creating a child process.
            printf("Failed creating child process(es) with code #: %s\nNow we are going back to the prompt!\n", strerror(errno));
            return ;
        }
        else if (pid == 0){ // child process
            // change the SIGINT handler to SIG_DFL, so that child processes can be terminated.
            signal(SIGINT, SIG_DFL);
            for (int j = 0; j < pipe_counter; j++) {
					if (i == 0) {
						close(fd[j][0]);
						if (j != i) {
							close(fd[j][1]);
						}
					}
					else if (i == command_counter - 1) {
						if (j != i-1) {
							close(fd[j][0]);
						}
						close(fd[j][1]);
					}
					else {
						if (j != i-1) {
							close(fd[j][0]);
						}
						if (j != i) {
							close(fd[j][1]);
						}
					}
				}
            if (command_counter == 1) {
            }
            else if (i == 0) {
                dup2(fd[i][1], STDOUT_FILENO);
                close(fd[i][1]);
            }
            else if (i == command_counter - 1) {
                dup2(fd[i-1][0], STDIN_FILENO);
                close(fd[i-1][0]);
            }
            else {
                dup2(fd[i][1], STDOUT_FILENO);
                close(fd[i][1]);
                dup2(fd[i-1][0], STDIN_FILENO);
                close(fd[i-1][0]);
            }

            // sigusr1, so that jcshell send the sigusr1 to activate the process execution
            while (child_ready == 0){
                usleep(10);
            }

            /*
            parse the command stored in command
            */
            char *args[MAX_COMMAND_LENGTH];
            int j = 0;
            args[j] = strtok(piped_commands[i], " \n");
            while (args[j] != NULL){
                j++;
                args[j] = strtok(NULL, " \n");
            }
            args[j] = NULL;

            if (execvp(args[0], args) == -1){
                // if there is an error executing execvp(), the following codes will be executed.
                command_counter = 0;
                printf("JCshell: %s: %s", args[0], strerror(errno));
                exit(-1);
            }
            exit(0);
        }

        else{ // parent process
            child_pid[i] = pid;
            if (command_counter == 1){
                // do nothing!
            }
            else if (i == 0){
                close(fd[i][1]);
            }
            else if (i == command_counter - 1){
                close(fd[i-1][0]);
            }
            else{
                close(fd[i-1][0]);
                close(fd[i][1]);
            }

            
            if (!blocked){
                sigemptyset(&blkset);
                sigaddset(&blkset, SIGINT);
                sigprocmask(SIG_BLOCK, &blkset, NULL);
                blocked = 1;
            }
            // send the SIGUSR1 for the child process, so that they can start their execution.
            kill(pid, SIGUSR1);
            siginfo_t info;
            // waiting for the termination of chid processes, put them into zombie state to get the running stat data from /proc/
            int ret = waitid(P_PID, pid, &info, WNOWAIT | WEXITED);
            if (!ret){
                runningStat(info.si_pid, info.si_status, i);
                waitpid(info.si_pid, &info.si_status, 0);
            }
        }
    }
    // since all of child processes are terminated and released from zombie state, we print out the running stat information.
    for (int i = 0; i < command_counter; i ++){
        printf("%s\n", run_stat[i]);
        snprintf(run_stat[i], MAX_COMMAND_LENGTH, "%s", "");
    }
    // unblock the SIGINT for the parent process
    sigprocmask(SIG_UNBLOCK, &blkset, NULL);
}

/*
runningStat() function has the following tasks:
- get the running data of the child process before they are removed from /proc/<pid>/stat and /proc/<pid>/status
- save the result string in the stat buffer so that it can be printed in the future, i.e., after child processes are released from the zombie state.
*/
void runningStat(pid_t pid, int status, int ind)
{
    char proc_path_stat[50];
    char *proc_stat[52];
    char proc_path_sche[50];
    char line_stat[MAX_COMMAND_LENGTH];
    char line_sche[MAX_COMMAND_LENGTH];

    sprintf(proc_path_stat, "/proc/%d/stat", pid);
    sprintf(proc_path_sche, "/proc/%d/sched", pid);


    //proc info
    // pid: stat-1
    // cmd: stat-2
    // state: stat-3
    // exit code: stat-52 --> need to get the which signal terminates the process
    // parent pid : stat-4
    // time in user mode: stat-14
    // time in kernal mode: stat-15

    // voluntary context switches: from status
    // non-voluntary context switches: from status

    long unsigned vctxt, nvctxt;

    FILE *file_sche = fopen(proc_path_sche, "r");
    if (file_sche == NULL){
        printf("schedule file Not Exists\n");
    }

    while (fgets(line_sche, sizeof(line_sche), file_sche)){
        if (strncmp(line_sche, "nr_voluntary_switches", strlen("nr_voluntary_switches")) == 0){
            sscanf(line_sche, "nr_voluntary_switches : %lu", &vctxt);
        }
        else if (strncmp(line_sche, "nr_involuntary_switches", strlen("nr_involuntary_switches")) == 0){
            sscanf(line_sche, "nr_involuntary_switches : %lu", &nvctxt);
        break;
        }
    }
    fclose(file_sche);



    // // get the information from /proc/{pid}/stat
    FILE *file_stat = fopen(proc_path_stat, "r");
    if (file_stat == NULL){
        printf("stat file Not Exists\n");
    }

    fgets(line_stat, sizeof(line_stat), file_stat);
    fclose(file_stat);
    
    int i = 0;
    char *stat = strtok(line_stat, " ");
    while (stat != NULL){
        if (i == 1)
        {
            char trimmedCMD[MAX_COMMAND_LENGTH];
            for (int j = 1; j < strlen(stat) - 1 ; j++){
                trimmedCMD[j-1] = stat[j];
            }
            trimmedCMD[strlen(stat)-2] = '\0';
            proc_stat[i++] = trimmedCMD;
            stat = strtok(NULL, " ");
        }
        else if (i == 51){
            stat[strlen(stat)-1]= '\0';
            proc_stat[i++] = stat;
            stat = strtok(NULL, " ");
        }
        else
        {
        proc_stat[i++] = stat;
        stat = strtok(NULL, " ");
        }
    }
    if (strcmp(proc_stat[1], "main") == 0){
        return;
    }

    char *err;
    if (status >= 32) {
        err = malloc(strlen("Killed")+1);
        strcpy(err, "Killed");
    }
    else {
        err = malloc(strlen(strsignal(status))+1);
        strcpy(err, strsignal(status));
    }
    // psignal(atoi(proc_stat[51]), err);
    if (status == 0)
    {
        // fgets(run_stat[ind], MAX_COMMAND_LENGTH,"\n(PID)%s (CMD)%s (STATE)%s (EXCODE)%s (PPID)%s (USER)%0.2lf (SYS)%0.2lf (VCTX)%lu (NVCTX)%lu",
        //     proc_stat[0], proc_stat[1], proc_stat[2], proc_stat[51], proc_stat[3], strtoul(proc_stat[13], NULL, 0)*1.0f/sysconf(_SC_CLK_TCK), strtoul(proc_stat[14], NULL, 0)*1.0f/sysconf(_SC_CLK_TCK) ,vctxt, nvctxt);
        sprintf(run_stat[ind], "(PID)%s (CMD)%s (STATE)%s (EXCODE)%s (PPID)%s (USER)%0.2lf (SYS)%0.2lf (VCTX)%lu (NVCTX)%lu",
        proc_stat[0], proc_stat[1], proc_stat[2], proc_stat[51], proc_stat[3], strtoul(proc_stat[13], NULL, 0)*1.0f/sysconf(_SC_CLK_TCK), strtoul(proc_stat[14], NULL, 0)*1.0f/sysconf(_SC_CLK_TCK), vctxt, nvctxt);
    } 
    else
    {
        // char *signal_name[MAX_COMMAND_LENGTH] = strsignal(atoi(proc_stat[51]));
        sprintf(run_stat[ind], "\n(PID)%s (CMD)%s (STATE)%s (EXSIG)%s (PPID)%s (USER)%0.2lf (SYS)%0.2lf (VCTX)%lu (NVCTX)%lu",
            proc_stat[0], proc_stat[1], proc_stat[2], err, proc_stat[3], strtoul(proc_stat[13], NULL, 0)*1.0f/sysconf(_SC_CLK_TCK), strtoul(proc_stat[14], NULL, 0)*1.0f/sysconf(_SC_CLK_TCK) ,vctxt, nvctxt);

    }
    free(err);
}
