#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <netdb.h>
#include <ctype.h>
#include "dir.h"
#include "usage.h"


int clientd;
char buffer[1024];
int length;
int is_login = 0;
int is_pasv_socket=0;
int nlst_socketd=-1;
int retr_socketd=-1;
int pasv_port,pasv_socket;
char* mes;

char serverDirectory[512];
char currentDirectory[512];

//print server response
int send_message(char* mes){
    //return length = snprintf(buffer, 1024, "%s\r\n", mes);
    return send(clientd, mes,strlen(mes) , 0);
}

//send error code
int sendResponse(int code){
    char* errorResponse;
    switch(code){
        case 150:
            errorResponse = "150 Here comes the directory listing.\r\n";
            break;
        case 200:
            errorResponse = "200 Command okay.\r\n";
            break;
        case 220:
            errorResponse = "220 Welcome.\r\n";
            break;
        case 221:
            errorResponse = "221 Goodbye.\r\n";
            break;
        case 226:
            errorResponse = "226 Directory send OK. Closing data connection.\r\n";
            break;
        case 230:
            errorResponse = "230 User logged in, proceed.\r\n";
            break;
        case 425:
            errorResponse = "425 Can't open data connection.\r\n";
            break;
        case 421:
            errorResponse = "421 Timeout. Closing data connection.\r\n";
            break;
        case 430:
            errorResponse = "430 Invalid username or password.\r\n";
            break;
        case 500:
            errorResponse = "500 Syntax error, command unrecognized and the requested action did not take place.\r\n";
            break;
        case 501:
            errorResponse = "501 Syntax error in parameters or arguments.\r\n";
            break;
        case 504:
            errorResponse = "504 Command not implemented for that parameter.\r\n";
            break;
        case 530:
            errorResponse = "530 Not logged in with CS317.\r\n";
            break;
        case 5301:
            errorResponse = "530 This FTP server is CS317 only.\r\n";
            break;
        case 5302:
            errorResponse = "530 Already logged in.\r\n";
            break;
        case 550:
            errorResponse = "550 Requested action not taken. File or directory unavailable. (e.g. file not found, no access).\r\n";
            break;
        default:
            errorResponse = "500 Syntax error, command unrecognized and the requested action did not take place.\r\n";
            break;
    }
    return send_message(errorResponse);
}


void* interact(void* args)
{
    // Interact with the client
    clientd = *(int*) args;
    getcwd(serverDirectory, 512);


    if(sendResponse(220) < 0){
        perror("Fail to log in");
        exit(-1);
    }

    // Receive the client message
    while (true)
    {
        bzero(buffer, 1024);
        ssize_t length = recv(clientd, buffer, 1024, 0);
        if (length < 0)
        {
            perror("Failed to read from the socket");
            break;
        }

        if (length == 0)
        {
            printf("EOF\n");
            break;
        }

        //check the input
        const char s[5] = " \n\r";


        char* argumentsArray = strtok(buffer,s);
        //after clear all space and enter
        if(argumentsArray == NULL){
            printf("empty input, try again\n");
            continue;
        }

        //store client input as array
        char* clientResponse[1024];
        int i=0;
        int numberOfArgument=0;
        while(argumentsArray!=NULL){
            clientResponse[i++]=argumentsArray;
            argumentsArray=strtok(NULL,s);
            numberOfArgument++;
        }



        if(strcasecmp(clientResponse[0],"QUIT")==0){

                sendResponse(221);
                is_login = 0;
                is_pasv_socket=0;
                close(clientd);
               //exit(-1);


        }else if(strcasecmp(clientResponse[0],"USER")==0){
            printf("# of argument: %d\n",numberOfArgument);
            printf("input: %s\nlength: %d\n",clientResponse[1],strlen(clientResponse[1]));
            //printf("here");
            if(is_login == 1){
                //printf("1");
                sendResponse(5302);

            }else if(strcasecmp(clientResponse[1],"CS317")==0){
                //CS317 230 Login successful.
                is_login=1;
                sendResponse(230);

            }else{
                printf("%s\n %d\n",clientResponse[1],strlen(clientResponse[1]));
                sendResponse(5301);

            }

        }else if(strcasecmp(clientResponse[0],"PASV")==0){
            printf("in pasv\n");
            printf("# of argument: %d\n",numberOfArgument);
            if(is_login == 1){
                if(is_pasv_socket==1){
                    //existing pasv socket,then close it
                    close(pasv_socket);
                }

                //then create PASV
                pasv_socket = socket(PF_INET,SOCK_STREAM,0);
                is_pasv_socket=1;

                if(pasv_socket==-1){
                    perror("Failed to create the socket.");
                    sendResponse(425);
                    continue;
                }
                printf("pasv_socket created\n");


                struct sockaddr_in pasv_address;

                bzero(&pasv_address, sizeof(struct sockaddr_in));
                pasv_address.sin_family = AF_INET;
                pasv_address.sin_port = 0;
                //pasv_address.sin_port = htons(pasv_port);
                pasv_address.sin_addr.s_addr = htonl(INADDR_ANY);
                //pasv_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);


                //
                if (bind(pasv_socket, (const struct sockaddr*) &pasv_address, sizeof(struct sockaddr_in)) != 0)
                {
                    perror("Failed to bind the socket");
                    exit(-1);
                }
                printf("pasv_socket bind done\n");

                //listen
                if(listen(pasv_socket,10)!=0){
                    perror("Failed to listen for pasv_connections");
                    send_message("500 Error entering Passive mode.\r\n");
                    exit(-1);
                }

                printf("pasv_socket listen done\n");

                //get port number: update pasv_address
                socklen_t len = sizeof(pasv_address);
                if(getsockname(pasv_socket, (struct sockaddr*) &pasv_address,&len)==-1){
                    perror("fail to getSockname!");
                    continue;
                }


                printf("after getsockname1\n");

                //get IP addr from hostent
                char hostBuffer[256];
                char *ipBuffer;
                struct hostent *host_entry;

                int hostname = gethostname(hostBuffer, sizeof(hostBuffer));
                printf("after getsockname2\n");

                host_entry=gethostbyname(hostBuffer);
                printf("after getsockname3\n");
                ipBuffer = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));




                uint32_t t = pasv_address.sin_addr.s_addr;
                pasv_port = ntohs(pasv_address.sin_port);
                int por1 = pasv_port>>8;
                int por2 = pasv_port & 0xff;

                //get ipbuffer
                char *ip[1024]; 
                char *eachIp = strtok (ipBuffer, ".");
                int b = 0;
                while (eachIp != NULL)
                {
                  ip[b++] = eachIp;
                  eachIp = strtok (NULL, ".");
                }
                printf("227 Entering passive mode (%s,%s,%s,%s,%d,%d)\n",
                    ip[0],ip[1],ip[2],ip[3],por1,por2);
                char messageWithIp[80];
                char* a = "227  Entering Passive Mode (%s,%s,%s,%s,%d,%d)\n" ;
                sprintf(messageWithIp,a,ip[0],ip[1],ip[2],ip[3],por1,por2);


                printf("after send mes\n");

                send_message(messageWithIp);

                printf("done pasv\n");


            }else{
                //please login
                sendResponse(530);
            }


        }else if(strcasecmp(clientResponse[0],"NLST")==0){
            //NLST with no command parameters
            printf("in nlst\n");
            printf("# of argument: %d\n",numberOfArgument);
                if(is_login==1){
                //check PASV
                if(is_pasv_socket==1){

                    if(nlst_socketd!=-1)
                        close(nlst_socketd);
                    struct sockaddr_in nlstAddress;
                    socklen_t nlstAddressLength = sizeof(struct sockaddr_in);
                    nlst_socketd = accept(pasv_socket, (struct sockaddr*) &nlstAddress, &nlstAddressLength);


                    printf("nlst: sock accept\n");

                    if(nlst_socketd <0){
                        sendResponse(425);
                        continue;
                    }

                    //successful accept
                    sendResponse(150);

                    char curDir[1024];
                    memset(curDir, 0, 1024);
                    getcwd(curDir, 1024);
                    listFiles(nlst_socketd, curDir);
                    close(nlst_socketd);
                    close(pasv_socket);
                    is_pasv_socket=0;
                    sendResponse(226);

                }else{
                    char* error_pasv_first ="425 Use PASV first.\r\n";
                    send_message(error_pasv_first);
                }
            }else{
                //please login
                sendResponse(530);
            }

        }else if(strcasecmp(clientResponse[0],"MODE")==0){
            if(is_login!=1){
                sendResponse(530);
            }else{
                if (numberOfArgument != 2) {
                    sendResponse(501);
                } else {
                    if(strcasecmp(clientResponse[1],"S")==0){
                        mes="200  Mode is valid. Mode set to Stream.\n";
                        send_message(mes);
                    }else if(strcasecmp(clientResponse[1],"B")==0||strcasecmp(clientResponse[1],"C")==0){
                        mes="504 Only Stream mode is supposed.\n";
                        send_message(mes);
                    }else{
                        mes="501  Bad command. Unknown MODE.\n";
                        send_message(mes);
                    }

                }
            }


        }else if(strcasecmp(clientResponse[0],"STRU")==0){
            if(is_login==1){
                if(strcasecmp(clientResponse[1],"F")==0){
                    mes="200 Successfully set structure to FILE.\n";
                    send_message(mes);
                }else if(strcasecmp(clientResponse[1],"R")==0||strcasecmp(clientResponse[1],"P")==0){
                    mes="504 - Other structure is not supported, only File.\n";
                    send_message(mes);
                }else{
                    mes="501  Bad command. Unknown structure.\n";
                    send_message(mes);
                }

            }else{
                //please login
                sendResponse(530);
            }



        }else if(strcasecmp(clientResponse[0], "TYPE") == 0){
            if (is_login != 1) {
                sendResponse(530);
            }else{

                if (numberOfArgument != 2) {
                    sendResponse(501);
                } else {

                    if ((strcasecmp(clientResponse[1], "A"))==0) {
                        mes="200 Successfully set type to A.\n";
                        send_message(mes);
                    } else if((strcasecmp(clientResponse[1], "I"))==0) {
                        mes="200 Successfully set type to I.\n";
                        send_message(mes);
                    //other type
                    }else if((strcasecmp(clientResponse[1], "E"))==0 || (strcasecmp(clientResponse[1], "L"))==0){
                        mes="504 - Other type is not supported, only ASCII and Image.\n";
                        send_message(mes);
                    }else{
                         mes="501  Bad command. Unknown type.\n";
                         send_message(mes);
                    }

                }
            }
        } else if (strcasecmp(clientResponse[0],"CWD") == 0) {
            printf("in cwd\n");
            if (is_login != 1) {
                sendResponse(530);
            } else {
                 if (numberOfArgument != 2) {
                    sendResponse(501);
                } else {
                    // if (((strncmp(clientResponse[1], "./", 2)) != 0) || ((strncmp(clientResponse[1], "../", 3)) != 0)) {
                    if(strstr(clientResponse[1], "./") != 0 || strstr(clientResponse[1], "../") != 0) {
                        sendResponse(550);
                     } else {
                        if (chdir(clientResponse[1]) == 0) {  //
                             getcwd(currentDirectory, 512);
                             mes="200 Directory changed\n";
                             send_message(mes);
                        } else if (chdir(clientResponse[1])!= 0) {
                            sendResponse(550);
                        } else {
                            sendResponse(501);
                     }
                   }
                }
            }

        }else if (strcasecmp(clientResponse[0], "CDUP") == 0) {
            if (is_login != 1) {
                    sendResponse(530);
                } else {
                     if (numberOfArgument != 1) {
                        sendResponse(501);
                    } else {
                        char temp[512];
                        getcwd(currentDirectory,512);           // getcwd(currDirectory, 512)
                        if (strcasecmp(currentDirectory, serverDirectory) == 0) {
                            sendResponse(550);
                        } else {
                        chdir("../");
                        getcwd(currentDirectory, 512);
                        sendResponse(200);
                        }
                    }
                }

        }else if (strcasecmp(clientResponse[0], "RETR") == 0) {
                printf("# of argument: %d\n",numberOfArgument);

//                char nameOfFile[1024];
//                strcpy(nameOfFile, clientResponse[1]);

                printf("file name: %s\n",clientResponse[1]);


                if(numberOfArgument!=2){
                    sendResponse(501);
                    continue;
                }

                if(is_login==1){
                //check PASV
                if(is_pasv_socket==1){
                    if(retr_socketd!=-1)
                        close(retr_socketd);

                    struct sockaddr_in retrAddress;
                    socklen_t retrAddressLength = sizeof(struct sockaddr_in);
                    retr_socketd = accept(pasv_socket, (struct sockaddr*) &retrAddress, &retrAddressLength);

                    if(retr_socketd <0){
                        sendResponse(425);
                        continue;
                    }
                    printf("retr sock created\n");


                    //check
                    struct stat fileStat;
                    //check file exit or not
                    printf("file name: %s\n",clientResponse[1]);

                    //get stat of file
                    int is_file_exit = stat(clientResponse[1],&fileStat);
//                    bzero(buffer,1024);

                        if(retr_socketd>=0 ){

                        printf("read file ing\n");
                            //read file

                            FILE* file;
                            printf("file name before open: %s\n",clientResponse[1]);
                            file = fopen(clientResponse[1],"r");

                            if (!file) {
                                sendResponse(550);
                            } else {

                                printf("file open ok\n");
                                char c[4096];
                                bzero(c,4096);

                                int numOfElements=fread(c,1,sizeof(c),file);
                                //to retr socket
                                write(retr_socketd,c,numOfElements);
                                printf("inside: %s + %d\n",c,numOfElements);

                                //free
                                bzero(c,4096);
                                fclose(file);
                                //close socket
                                close(retr_socketd);
                                close(pasv_socket);
                                is_pasv_socket=0;
                                //is_file_exit=-1;

                                char retrMessage[1024];
                                int fileSize = fileStat.st_size;
                                char* a = "150 Opening BINARY mode data connection for %s (%d bytes).\n" ;
                                sprintf(retrMessage,a,clientResponse[1],fileSize);
                                send_message(retrMessage);
                                mes="226 Transfer complete. Closing the data connection.\n";
                                send_message(mes);

                            }

                        }

                }else{
                    char* error_pasv_first ="425 Use PASV first.\r\n";
                    send_message(error_pasv_first);
                }
            }else{
                //please login
                sendResponse(530);
            }


        }else{
            send_message("500 - Unknown command.\n");
        }

    }
    close(clientd);

    return NULL;
}


int main(int argc, char **argv) {

    //get port num
    if(argc != 2){
        perror("Only need to input a port number!");
        exit(-1);
    }
    int potNumber= atoi(argv[1]);
    printf("port num: %d \n", potNumber);


    //create TCP socket
    int server_socket = socket(PF_INET,SOCK_STREAM,0);

    if(server_socket==-1){
        perror("Failed to create the socket.");
        sendResponse(425);
        exit(-1);
    }
    printf("socket created");



    struct sockaddr_in address;
    bzero(&address, sizeof(struct sockaddr_in));
    address.sin_family = AF_INET;
    address.sin_port = htons(potNumber);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);




    //bind
    if (bind(server_socket, (const struct sockaddr*) &address, sizeof(struct sockaddr_in)) != 0)
    {
        perror("Failed to bind the socket");
        exit(-1);
    }

    printf("bind done\n");

    //listen
    if(listen(server_socket,10)!=0){
        perror("Failed to listen for connections");
        exit(-1);
    }

    //connect
    while(true){
        struct sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(struct sockaddr_in);
        printf("Waiting for incomming connections...\n");
        //accept
        int clientd = accept(server_socket, (struct sockaddr*) &clientAddress, &clientAddressLength);
        if(clientd <0){
            perror("Failed to accept the client connection");
            continue;
        }
        printf("Accepted the client connection from %s:%d.\n", inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));

        // Create a separate thread to interact with the client
        pthread_t thread;

        if (pthread_create(&thread, NULL, interact, &clientd) != 0)
        {
            perror("Failed to create the thread");
            continue;
        }

        // The main thread just waits until the interaction is done
        pthread_join(thread, NULL);
        printf("Interaction thread has finished.\n");
    }

    return 0;

}


