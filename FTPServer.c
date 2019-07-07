/*
 *  Main program of the ftp server. Creates a stream socket and
 *  listens on the provided port. Accepts one client at a time.
 *  Continues listening after finishes serving the current client.
 *  And accepts the clients first come being served basis.
 *  Accepted commands are:
 *  USER, QUIT, CWD, CDUP, TYPE, MODE, SRU, RETR, PASV, NLST. 
 *  Notes: 
 *  - The server will respond with 500 to any other commands that
 *    are not listed here. 
 *  - The server will not accept the commands requesting to go the
 *    parent directory of where the ftp server has started from.
 *  - The functionality that each of these commands provide can be found
 *    on: https://tools.ietf.org/html/rfc959
 *
 *  Author: Ege Okten
 *  Modified: 2019-05-01
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "dir.h"
#include "usage.h"
#include "netbuffer.h"
#include "util.h"
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <stdarg.h>
#include <signal.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>

#define BUFFER_SIZE 256
#define BACKLOG 5     // how many pending connections queue will hold
#define MAX_LINE_LENGTH 1024 /* Maximum line length for the ftp communication */
#define MAX_PATH_LENGTH 1024 /* Maximum path length for changing the directory*/

net_buffer_t communicationBuffer; /* The special buffer to be used for the communication with the client */
char buf[MAX_LINE_LENGTH + 1]; /* Char array buffer to hold lines read from the socket */
int result_read_line; /* integer to check the result of nb_read_line function */
int controlcon_file_descriptor = -1; /* file descriptor of the main ftp socket */
char main_dir[MAX_PATH_LENGTH + 1] = ""; /* path to the main working directory, initialized when handle_client starts */
int logged_in = 0; /* integer to check if the user has been logged in correctly; if yes 1, 0 otherwise */
int passive_mode = 0; /* integer to check if the passive mode has been activated */
int cur_command_num_arg = 0;
int pasv_init_descriptor = -1; /* file descriptor of the initial pasv call socket */
int datacon_file_descriptor = -1; /* file descriptor of the passive mode ftp socket */
char current_command[BUFFER_SIZE] = ""; /* buffer to hold current command */
char current_command_arg[BUFFER_SIZE] = ""; /* buffer to hold current argument for current command*/


static void handle_client(char * port);
static void string_to_upper(char * string);
static void handle_user(char * command_argument);
static void handle_quit();
static void handle_cwd(char * command_argument);
static void handle_cdup();
static void handle_pasv();
static void handle_type(char * command_argument);
static void handle_stru(char * command_argument);
static void handle_mode(char * command_argument);
static void handle_retr(char * command_argument);
static void handle_nlst();
void replace_line_from_string(char * str);
static int is_using_illegal_cwd(char * path);
int create_com_socket(const char * port);
static void *get_in_addr(struct sockaddr *sa);
int create_data_socket();
int activate_data_connection();
void parse_command(char * str);
void close_data_con_resources();
void close_resources();

// Here is an example of how to use the above function. It also shows
// one how to get the arguments passed on the command line.

int main(int argc, char *argv[])
{
    // Check the command line arguments
    if (argc != 2) {
      usage(argv[0]);
      return -1;
    }
    
    
    while (1) {
    
    handle_client(argv[1]);
        
    }
}

/*
 *  handle_client(port)
 * 
 *  Handles the communication with the client.
 *  Creates a stream socket on port and starts listening for
 *  a client by calling create_com_socket. After the initial
 *  communication with the client, it starts reading the commands
 *  sent by the client and for each command it parses the command
 *  by calling parse_command(), then it calls necessary handlers
 *  for that command.
 */
static void
handle_client(port)
char * port; /* portnumber */
{
    // remember the main connection file descriptor
    // controlcon_file_descriptor = run_server(port);
    controlcon_file_descriptor = create_com_socket(port);
    
    // save the starting working directory
    getcwd(main_dir, sizeof(main_dir));
    
    // start communicating by asking for a username
    send_string(controlcon_file_descriptor, "220 Welcome. Server is ready. Provide a username. \r\n");

    // initialize the communication buffer to be used in sending/receiving data from socket
    int result;
    communicationBuffer = nb_create(controlcon_file_descriptor, MAX_LINE_LENGTH + 1);
    
    while (1) {
        
        // read and process next command
        result = nb_read_line(communicationBuffer, buf);
        if (result == -1) { // netbuffer couldn't read; connection error
            perror("server: error on reading data on control connection.");
            close_resources();
            return;
        }
        if ( result == 0) { // client left
            perror("server: client left.");
            close_resources();
            return;
        }
        
        // take off the CRLF before parsing the command
        replace_line_from_string(buf);
        // parse the command and args
        parse_command(buf);
        // for case-insensitive check
        string_to_upper(current_command);
        
        
        if (!strcmp("USER",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
               handle_user(current_command_arg);
            }
            
            
        } else if (!strcmp("QUIT",current_command)) {
            if (cur_command_num_arg != 0) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_quit();
                return;
            }
            
            
        } else if (!strcmp("CWD",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
               send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
               handle_cwd(current_command_arg);
            }
            
            
        } else if (!strcmp("CDUP",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_cdup();
            }
            
        } else if (!strcmp("PASV",current_command)) {
            if (cur_command_num_arg != 0) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_pasv();
            }
            
        } else if (!strcmp("TYPE",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_type(current_command_arg);
            }
            
        } else if (!strcmp("STRU",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_stru(current_command_arg);
            }
            
        } else if (!strcmp("MODE",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_mode(current_command_arg);
            }
            
        } else if (!strcmp("RETR",current_command)) {
            if (cur_command_num_arg != 1) { // incorrect call
                send_string(controlcon_file_descriptor, "501 Syntax error, verify your input.\r\n");
            } else {
                handle_retr(current_command_arg);
            }
            
        } else if (!strcmp("NLST",current_command) || !strcmp("LIST",current_command)) {
            if (cur_command_num_arg == 1) { // incorrect call
                send_string(controlcon_file_descriptor,
                            "502 NLST with arguments not implemented.\r\n");
            } else if (cur_command_num_arg == 0) {
                handle_nlst();
            } else {
                send_string(controlcon_file_descriptor, "501 Syntax error.\r\n");
            }
            
            
        } else {
            send_string(controlcon_file_descriptor, "500 Syntax error, command unrecognized.\r\n");
        }
    }
}

/*
 *  handle_user(command_argument)
 *
 *  Handles USER command: only accepted username is cs317 (case insensitive).
 *  sends back the necessary responses to the client after checking username
 */
static void
handle_user(command_argument)
char * command_argument;
{
    char * username = "CS317";
    string_to_upper(command_argument); // get the uppercase so check with case-insensitive
    if (!command_argument) {
        send_string(controlcon_file_descriptor, "530 Incorrect username, not logged in.\r\n");
    } else if (!strcmp(username, command_argument)) {  // username is cs317, correct
        logged_in = 1; // authorized user logged in
        send_string(controlcon_file_descriptor, "230 User logged in, proceed.\r\n");
    } else { // not a valid username
        send_string(controlcon_file_descriptor, "530 Incorrect username, not logged in.\r\n");
    }
}

/*
 *  handle_quit()
 *
 *  Handles the QUIT command: closes all the sockets in use
 */
static void
handle_quit()
{
    send_string(controlcon_file_descriptor, "221 Bye.\r\n");
    close_resources();
}

/*
 *  handle_cwd(command_argument)
 *
 *  Handles CWD command
 *
 *  Note: For security reasons, CWD command that starts with "./", ".", "../". ".."
 *  or contains "../" in it and commands that have absolute paths that don't start
 *  from the root directory of the server will not be accepted.
 */
static void
handle_cwd(command_argument)
char * command_argument;
{
    if (logged_in) {
        if(!command_argument) { // syntax error in parameters, it should have given a path
            send_string(controlcon_file_descriptor, "501 Syntax error, a path is expected.\r\n");
        } else if (is_using_illegal_cwd(command_argument)) { // check if it is legal according to the note above
            send_string(controlcon_file_descriptor, "550 Action not permitted.\r\n");
        } else { // try to change the directory
            int result = chdir(command_argument);
            if (!result) {
                send_string(controlcon_file_descriptor, "250 Directory change has been completed.\r\n");
            } else if (result == EACCES) {
                send_string(controlcon_file_descriptor, "550 Action not taken, no permission.\r\n");
            } else {
                send_string(controlcon_file_descriptor, "550 No such file or directory.\r\n");
            }
        }
    } else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  handle_cdup()
 *
 *  Handles CDUP command
 *
 *  Note: For security reasons, CDUP command to set the working directory to
 *  be the parent directory of where the ftp server has started from will not
 *  be accepted.
 */
static void
handle_cdup()
{
    if (logged_in) {
        char cwd[MAX_PATH_LENGTH + 1];
        
        if(getcwd(cwd, sizeof(cwd)) != NULL) {
            if(!strcmp(cwd, main_dir)) { // cannot go to the parent of initial starting dir
                send_string(controlcon_file_descriptor, "550 Action not taken, no permission.\r\n");
                
            } else {
                int result = chdir("..");
                if (!result) { // change has been successful
                    send_string(controlcon_file_descriptor, "200 Directory has been change to the parent.\r\n");
                } else if (result == EACCES) { // don't have access
                    send_string(controlcon_file_descriptor, "550 Action not taken, no permission.\r\n");
                } else { // some other error occured
                    send_string(controlcon_file_descriptor, "550 Action cannot be taken.\r\n");
                }
            }
            
         } else {
            send_string(controlcon_file_descriptor, "421 Not available, try again later.\r\n");
         }
        // not logged in
    } else {
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
    }
}

/*
 *  handle_pasv()
 *
 *  Handles PASV command by calling a helper function to create
 *  another socket for the data connection. Sending Pasv command will
 *  close the current one and will try to open up a new one.
 */
static void
handle_pasv()
{
    if (logged_in) {
        if(!passive_mode) {
        
            int result = create_data_socket();
            if (result == -1) {
                // error occured and the connection info has not been sent
                send_string(controlcon_file_descriptor,
                            "421 Service not available, closing control connection.\r\n");
                close(result);
                pasv_init_descriptor = - 1;
                passive_mode = 0;
            } else {
                pasv_init_descriptor = result;
                passive_mode = 1;
            }
            
        } else { // already in passive mode; close the old connection and open a new one
            pasv_init_descriptor = -1;
            datacon_file_descriptor = 0;
            passive_mode = 0;
            handle_pasv();
        }
    } else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  handle_type(command_argument)
 *
 *  Handles TYPE command: changes the binary flag of the server,
 *  sends necessary responses to the client
 *
 *  Note: this implementation only accepts Image & ASCII type
 */
static void
handle_type(command_argument)
char * command_argument; /* data type that is being requested */
{
    if (logged_in) {
        if ( !strcmp("I", command_argument) ||  !strcmp("A", command_argument)) {
            send_string(controlcon_file_descriptor, "200 Command okay.\r\n");
        } else if (!strcmp("L", command_argument) ||
                   (cur_command_num_arg ==3 && !strcmp("A", command_argument))) {
            send_string(controlcon_file_descriptor, "504 Not implemented.\r\n");
        } else {
            send_string(controlcon_file_descriptor, "501 Syntax error.\r\n");
        }
    } else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  handle_stru(command_argument)
 *
 *  Handles STRU command: accepts the F (file structure), rejects any other
 *  with 504 not implemented.
 */
static void
handle_stru(command_argument)
char * command_argument; /* structure mode that is being requested */
{
    if (logged_in) {
        if ( !strcmp("F", command_argument)) {
            send_string(controlcon_file_descriptor, "200 Command okay.\r\n");
        } else {
            send_string(controlcon_file_descriptor, "504 Not implemented.\r\n");
        }
    } else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  handle_mode(command_argument)
 *
 *  Handles MODE command: accepts the request with S; rejects any other commands
 *  with 504 not implemented.
 */
static void
handle_mode(command_argument)
char * command_argument; /* mode that is being requested */
{
    if (logged_in) {
        if ( !strcmp("S", command_argument)) {
            send_string(controlcon_file_descriptor, "200 Command okay.\r\n");
        } else {
            send_string(controlcon_file_descriptor, "504 Not implemented.\r\n");
        }
    } else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  handle_retr(command_argument)
 *
 *  Handles the RETR command
 *
 */
static void
handle_retr(command_argument)
char * command_argument; /* path to a file that is being requested */
{
    if (logged_in) {
        // check if it's in passive mode
        if(!passive_mode) {
            send_string(controlcon_file_descriptor,
                        "425 Can't open data connection. Enable passive first\r\n");
        } else { // can handle the command now
            
            // check access
            if (access(command_argument, R_OK) == -1) {
                if (errno == EACCES) {
                    send_string(controlcon_file_descriptor, "550 No access to the directory.\r\n");
                } else {
                   send_string(controlcon_file_descriptor, "550 File not found.\r\n");
                }
                // close all the sources and reset variables after error
                close_data_con_resources();
                
                // can access the file; handle retr
            } else {
                FILE * file = fopen(command_argument, "r");
                send_string(controlcon_file_descriptor,
                            "150 File status ok. About to open data connection for file: %s .\r\n", command_argument);
                
                // try to open data connection
                int socket_result = activate_data_connection();
                if (socket_result == -2) {
                    send_string(controlcon_file_descriptor,
                                "425 No connection was established.\r\n");
                    // close all the sources and reset variables after error
                    close_data_con_resources();
                    return;
                } else if (socket_result == -1) {
                    send_string(controlcon_file_descriptor,
                                "426 Connection failure.\r\n");
                    // close all the sources and reset variables after error
                    close_data_con_resources();
                    
                } else { // get the new socket fd on success
                    datacon_file_descriptor = socket_result;
                }
                
                // if here then connection is ready
                char data_buffer[BUFFER_SIZE * 2];  // data buffer to read and send data
                int bytes_read;
                int send_result;
                
                memset(&data_buffer, 0, sizeof data_buffer);
                while((bytes_read = fread(data_buffer, sizeof(char), (BUFFER_SIZE * 2), file)) > 0){
                    send_result = send_all(datacon_file_descriptor, data_buffer, bytes_read);
                    
                    if (send_result == -1) { // error occured while sending
                        send_string(controlcon_file_descriptor,
                                    "426 Connection failure.\r\n");
                        
                        // close all the sources and reset variables after error
                        close_data_con_resources();
                    }
                }
                
                // if here then the data successfully sent
                close_data_con_resources(); // close all the sources and reset variables after success
                send_string(controlcon_file_descriptor,
                            "226 Closing data connection. Requested file action successful.\r\n");
            }
        }
    } else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  handle_nlst()
 *
 *  Handles NLST command: sends the list of the files in the directory from an already
 *  established data connection by the client.
 *
 *  Note: this program doesn't implement NLST version that requires an argument.
 */
static void
handle_nlst()
{
    if (logged_in) {
        if (passive_mode) {
            
            char current_dir[MAX_PATH_LENGTH + 1];
            getcwd(current_dir, sizeof(current_dir));
            
            // check access
            if (access(current_dir, R_OK) != 0) {
                send_string(controlcon_file_descriptor, "550 No access to the directory.\r\n");
                // close all the sources and reset variables after error
                close_data_con_resources();
                return;
            }
            
            // try to open the data connection
            send_string(controlcon_file_descriptor,
                        "150 Directory status ok. About to open data connection.\r\n");
            int socket_result = activate_data_connection();
            if (socket_result == -2) {
                send_string(controlcon_file_descriptor,
                            "425 No connection was established.\r\n");
                // close all the sources and reset variables after error
                close_data_con_resources();
                return;
                
            } else if (socket_result == -1) {
                send_string(controlcon_file_descriptor,
                            "426 Connection failure.\r\n");
                // close all the sources and reset variables after error
                close_data_con_resources();
                return;
                
            } else {  // get the new descriptor on success
                datacon_file_descriptor = socket_result;
            }
            
            
            // connection ready; try to send the dir list.
            int result = listFiles(datacon_file_descriptor, current_dir);
            if (!result) { // no permission to access the directory
                // close all the sources and reset variables after error
                close_data_con_resources();
                send_string(controlcon_file_descriptor, "451 Cannot read the directory.\r\n");
            } else {
                // close all the sources and reset variables after success
                close_data_con_resources();
                send_string(controlcon_file_descriptor,
                            "226 Closing data connection. Requested file action successful.\r\n");
            }
        } else {
            send_string(controlcon_file_descriptor,
                        "425 Cannot open data connection. Must open a passive connection first.\r\n");
          }
    }else // cannot proceed before a authorized login
        send_string(controlcon_file_descriptor, "530 Not logged in.\r\n");
}

/*
 *  is_using_illegal_cwd(path)
 *
 *  Returns 1 if path starts with "./", ".", "../". ".."
 *  or contains "../" in it or if path is absolute(starts with /) and doesn't start
 *  from the root directory of the server. Returns 0 otherwise.
 */
static int /* TRUE OR FALSE */
is_using_illegal_cwd(path)
char * path;
{
    if (path[0] == '.' && path[1] == '/') return 1; // path starts with ./
    if (path[0] == '.' && path[1] == '.' && path[2] == '/') return 1; // path starts with ../
    if (!strcmp(".", path)) return 1;  // path is .
    if (!strcmp("..", path)) return 1; // path is ..
    
    
    char * dir_changer = "../";
    char * result = strstr(path, dir_changer);
    if(!result) return 0; // path contains ../
    
    
    if (path[0] == '/') {  // if the path is absolute then it should start with the server's root
        size_t len_main = strlen(main_dir);
        size_t len_path = strlen(path);
        
        int check = len_main > len_path ? 0 : strncmp(main_dir, path, len_main) == 0;
        if(!check) return 0;  // path is absolute but doesn't start with main_dir
    }
    
    
    return 1;
}

/*
 *  replace_line_from_string(str)
 *
 *  Replaces the first \n or \r with \0, returns rightaway if str is NULL.
 */
void replace_line_from_string(str)
char *str; /* String to be manipulated: change the first \n or \r with \0 */
{
    if(!str) return;
    int i = 0;
    int len = strlen(str)+1;
    
    for(i=0; i<len; i++)
    {
        if(str[i] == '\r' || str[i] == '\n')
        {
            str[i] = '\0';
            break;
        }
    }
}



/*
 *  get_arg_number(str)
 *
 *  Parses the string, gets the command verb(if any) and puts it on current_command,
 *  gets the argument (if any) and puts it on current_command_arg, and counts the
 *  number of arguments and puts it in current_command_num_arg.
 *
 *  Note: the str must be null ended string
 */
void
parse_command(str)
char *str; /* null ended command line string */
{
    cur_command_num_arg = 0;
    strcpy(current_command, "");
    strcpy(current_command_arg, "");
    
    if (!str) {
        return;
    }

    // get the command
    char * command = strtok(str, " ");
    if(command) {
        strcpy(current_command, command);
    }
    
    // get the argument
    char * argument = strtok(NULL, " ");
    if(argument) {
        strcpy(current_command_arg, argument);
        
    }
    
    // count the number of arguments
    while(argument != NULL){
        argument = strtok(NULL, " ");
        cur_command_num_arg ++;
    }
    
}

/*
 *  string_to_upper(string)
 *
 *  Converts the string into uppercase
 */
static void
string_to_upper(string)
char * string; /* pointer to null ending string */
{
    if(!string) return;
    char * s = string;
    while(*s){
        *s = toupper((unsigned char) *s);
        s++;
    }
}


/** Creates a server socket at the specified port number for the ftp
 *  communication, listens for new connections and accepts them.
 *
 *  Parameters: port: String corresponding to the port number (or
 *                    name) where the server will listen for new
 *                    connections.
 *
 */
int
create_com_socket(port)
const char * port;
{
    
    int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_INET;   // use IPv4 - can update to include ipv6 with AF_UNSPEC
    hints.ai_socktype = SOCK_STREAM; // create a stream (TCP) socket server
    hints.ai_flags    = AI_PASSIVE;  // use any available connection
    
    // Gets information about available socket types and protocols
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "control con getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        
        // create socket object
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("control connection: socket");
            continue;
        }
        
        // specify that, once the program finishes, the port can be reused by other processes
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("control con setsockopt");
            exit(1);
        }
        
        // bind to the specified port number
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            if (sockfd != 0)
                close(sockfd);
            perror("control connection: bind");
            continue;
        }
        
        // if the code reaches this point, the socket was properly created and bound
        break;
    }
    
    // all done with this structure
    freeaddrinfo(servinfo);
    
    // if p is null, the loop above could create a socket for any given address
    if (p == NULL)  {
        fprintf(stderr, "control connection: failed to bind\n");
        exit(1);
    }
    
    // sets up a queue of incoming connections to be received by the server
    if (listen(sockfd, BACKLOG) == -1) {
        perror("control connection listen");
        exit(1);
    }
    
    
    printf("server: waiting for connections...\n");
    
    //while(1) {
    // wait for new client to connect
    sin_size = sizeof(their_addr);
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if (new_fd == -1) {
        perror("accept");
        // continue;
    }
    
    
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr),
              s, sizeof(s));
    
    printf("server: got connection from %s\n", s);
    close(sockfd);
    return new_fd;
    
}


/** Returns the IPv4 or IPv6 object for a socket address, depending on
 *  the family specified in that address.
 */
static void
*get_in_addr(sa)
struct sockaddr *sa;
{
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    else
        return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 *  create_data_socket()
 *
 *  Creates a socket for the data communication with an available port, returns
 *  the initial file descriptor before starting to listen on the socket
 */
int
create_data_socket()
{
    
    // create the data structures to create and initialize the socket
    int sockfd, new_fd; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    struct sockaddr_in data_sock_addr;
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;
    
    
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("datasocket: socket");
        return -1;
    }
    
    memset(&data_sock_addr, 0, sizeof data_sock_addr);
    data_sock_addr.sin_family = AF_INET;  // use ipv4
    data_sock_addr.sin_port = 0;  // to get a random available dynamic port
    data_sock_addr.sin_addr.s_addr = INADDR_ANY;
    
    // specify that, once the program finishes, the port can be reused by other processes
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("datasocket: setsockopt");
        return -1;
    }
    
    // bind to the specified port number
    if (bind(sockfd, (struct sockaddr *) &data_sock_addr, sizeof(data_sock_addr)) == -1) {
        if (sockfd != 0)
            close(sockfd);
        perror("server: bind");
        return -1; // if the port is already in use, this function must be called with
                  // a new port number;
     }
    
    // provide ip and port information to the client
    
    // get the ip information from the control connection socket
    struct sockaddr my_addr;
    socklen_t addrlen = sizeof(my_addr);
    int res = getsockname(controlcon_file_descriptor, &my_addr, &addrlen);
    if (res == -1) return -1;
    
    // decode ip
    struct sockaddr_in *socket_info = (struct sockaddr_in *) &my_addr;
    struct in_addr * ipv4 = &socket_info->sin_addr;
    unsigned char * ip = (unsigned char *) ipv4;
    unsigned int h1 = (unsigned int) ip[0];
    unsigned int h2 = (unsigned int) ip[1];
    unsigned int h3 = (unsigned int) ip[2];
    unsigned int h4 = (unsigned int) ip[3];
    
    // get the port information after bind to data connection sockt
    struct sockaddr my_addr_port;
    addrlen = sizeof(my_addr_port);
    res = getsockname(sockfd, &my_addr_port, &addrlen);
    if (res == -1) return -1;
    
    // decode the information
    socket_info = (struct sockaddr_in *) &my_addr_port;
    unsigned short port = htons((unsigned int) socket_info->sin_port);
    
    unsigned short p1 = port/256;
    unsigned short p2 = port % 256;
    
    
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        return -1;
    }
    
    send_string(controlcon_file_descriptor, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\n", h1, h2, h3, h4, p1, p2);
    
    return sockfd;
}

/*
 *  activate_data_connection()
 *
 *  Listens and accepts on the data connection socket. If no connection occurs
 *  in 15 seconds it returns -2; it returns -1 for any other error. Otherwise it returns
 *  the file descriptor of the new socket.
 */
int
activate_data_connection()
{
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    int new_fd;
    
    printf("listening on %d\n", pasv_init_descriptor);

    
    // set up the timeout
    fd_set rfds;
    struct timeval time_val;
    int return_val;
    
    FD_ZERO(&rfds);
    FD_SET(pasv_init_descriptor, &rfds);
    
    // Wait up to 15 seconds
    time_val.tv_sec = 15;
    time_val.tv_usec = 0;
    return_val = select(pasv_init_descriptor + 1, &rfds, 0, 0, &time_val);
    sin_size = sizeof(their_addr);
    new_fd = accept(pasv_init_descriptor, (struct sockaddr *) &their_addr, &sin_size);
    if (new_fd < 0) {
        perror("error on data connection accept");
        return -1;
    }
    return new_fd;
}


/*
 *  close_data_con_resources()
 *
 *  Closes the sockets that are used for opening and maintaining
 *  data connection. Usually called after completing a passive connection
 *  or after an error when opening a data connection or during a data
 *  exchange over the data connection. Resets the variables back to their
 *  defaut values: passive_mode to 0 and passive con descriptors to -1.
 */
void
close_data_con_resources()
{
    passive_mode = 0;
    close(pasv_init_descriptor);
    close(datacon_file_descriptor);
    pasv_init_descriptor = - 1;
    datacon_file_descriptor = -1;
}

/*
 *  close_resources()
 *
 *  Closes all the resources that are in use including the communication/
 *  data sockets, and netbuffer sutructs.  Usually called after a quit
 *  command or a terminal error.
 */
void
close_resources()
{
    if (passive_mode){
        close(pasv_init_descriptor);
        close(pasv_init_descriptor);
        pasv_init_descriptor = - 1;
        datacon_file_descriptor = -1;
    }
    close(datacon_file_descriptor);
    nb_destroy(communicationBuffer);
}



