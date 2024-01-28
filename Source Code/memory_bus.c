#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>


#define PORT 7777
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 2

sem_t semaphore;

int client_sockets[MAX_CLIENTS] = {
  -1,
  -1
};
int client_sockets_count = 0;
int halt_count = 0;

// Structure of a variable that has variable's name and its corresponding value in Symbol Table, the address that it points to, socketToBeUpdated(it is initially -1 otherwise denotes which socket needs to change), operation that basically tells which operation was recently done on this
struct Variable {
  char name[50];
  int value;
  int address;
  int socketToBeUpdated;
  char operation;

};

// Structure for Global Variables Symbol Table that stores 1000 variables, table's size
struct GlobalVariableSymbolTable {
  struct Variable vars[1000];
  int size;
};

struct GlobalVariableSymbolTable symbolTable;

// Takes a variable name and returns the value of that variable in the symbol table
int getVariableValue(char varName[]) {
  for (int i = 0; i < symbolTable.size; i++) {
    if (strcmp(symbolTable.vars[i].name, varName) == 0) {
      return symbolTable.vars[symbolTable.vars[i].address].value;
    }
  }
  return -1;
}

// Adds the given variable to the given symbol table and sets its initial value to 0
void addVariableToSymbolTable(char * varName) {
  if (getVariableValue(varName) == -1) {
    strcpy(symbolTable.vars[symbolTable.size].name, varName);
    symbolTable.vars[symbolTable.size].value = 0;
    symbolTable.vars[symbolTable.size].address = symbolTable.size;
    symbolTable.vars[symbolTable.size].socketToBeUpdated = -1;
    symbolTable.size++;
  }
}

// Function to send an integer back to the client
void sendIntegerResponse(int client_socket, int value) {
  char response[BUFFER_SIZE];
  snprintf(response, BUFFER_SIZE, "%d", value); // Convert integer to string
  if (write(client_socket, response, BUFFER_SIZE) < 0) {
    perror("Sending response to client failed");
  }
}

// Function to send a string back to the client
void sendResponse(int socket, char message[]) {
  char response[BUFFER_SIZE];
  snprintf(response, BUFFER_SIZE, "%s", message);
  if (write(socket, response, BUFFER_SIZE) < 0) {
    perror("Sending response to client failed");
  }
}

// Executes the appropriate instruction received from the client
void executeInstruction(char * instruction, int client_socket) {
  char variables[50];
  char varName[50];
  int value;
  int num_variables = 0;

  // This instruction creates global variables here in the memory bus
  if (sscanf(instruction, " global_vars %99[^\n]", variables) == 1) {
    char * token = strtok(variables, " "); // Split by space
    while (token != NULL) {
      addVariableToSymbolTable(token);
      token = strtok(NULL, " ");
    }
    sendResponse(client_socket, "");
  }
   // This instruction reads and returns the global value of the given variable
   else if (sscanf(instruction, " bus_read %s", varName) == 1) {

    // Here we add the respective changes in the symbol table so that I, S & M states are changed in the other client
    if (client_socket == 4) {
      for (int i = 0; i < symbolTable.size; i++) {
        if (strcmp(symbolTable.vars[i].name, varName) == 0) {
          symbolTable.vars[i].socketToBeUpdated = 5;
          symbolTable.vars[i].operation = 'r';
        }
      }
    } else if (client_socket == 5) {
      for (int i = 0; i < symbolTable.size; i++) {
        if (strcmp(symbolTable.vars[i].name, varName) == 0) {
          symbolTable.vars[i].socketToBeUpdated = 4;
          symbolTable.vars[i].operation = 'r';
        }
      }
    }

    // Below steps are mandatory so that if P2 reads 'x', we let P1 run and get the changes that
    // we wrote above and then write to bus the new value of 'x' and then we return the value of 'x'
    // in global symbol table.

    // Unlock the semaphore so that other client can update the global value
    if (sem_post( & semaphore) == -1) {
      perror("sem_post failed");
      exit(1);
    }
    sleep(0.4);
    // Lock the semaphore so the current client can go back to return the global value
    if (sem_wait( & semaphore) == -1) {
      perror("sem_wait failed");
      exit(1);
    }

    sendIntegerResponse(client_socket, getVariableValue(varName));
  }

   // This instruction writes the given value to the given global variable in the global symbol table
   else if (sscanf(instruction, " bus_write %99[^\n]", variables) == 1) {
    char * token = strtok(variables, " "); // Split by space
    while (token != NULL) {
      if (num_variables == 1) {
        snprintf(varName, sizeof(varName), "%s", token);
        num_variables++;
      }
      if (num_variables == 0) {
        value = atoi(token);
        num_variables++;
      }
      token = strtok(NULL, " ");
    }
    for (int i = 0; i < symbolTable.size; i++) {
      if (strcmp(symbolTable.vars[i].name, varName) == 0) {
        symbolTable.vars[i].value = value;
        break;
      }
    }

    // Here we add the respective changes in the symbol table so that I, S & M states are changed in the other client
    if (client_socket == 4) {
      for (int i = 0; i < symbolTable.size; i++) {
        if (strcmp(symbolTable.vars[i].name, varName) == 0) {
          symbolTable.vars[i].socketToBeUpdated = 5;
          symbolTable.vars[i].operation = 'w';
        }
      }
    } else if (client_socket == 5) {
      for (int i = 0; i < symbolTable.size; i++) {
        if (strcmp(symbolTable.vars[i].name, varName) == 0) {
          symbolTable.vars[i].socketToBeUpdated = 4;
          symbolTable.vars[i].operation = 'w';
        }
      }
    }

    // Send an empty response as nothing needs to be returned
    sendResponse(client_socket, "");
  }

   // This instruction performs the :& operation on the global symbol table
  else if (sscanf(instruction, ":& %99[^\n]", variables) == 1) {
    char toBeChanged[50];
    char willChangeTo[50];
    int num_variables = 0;  // Initialize num_variables to zero

    if (sscanf(instruction, ":& %49s %49s", toBeChanged, willChangeTo) == 2) {

    int flag = 0;
    for (int i = 0; i < symbolTable.size; i++) {
        if (strcmp(symbolTable.vars[i].name, toBeChanged) == 0) {
            for (int j = 0; j < symbolTable.size; j++) {
                if (strcmp(symbolTable.vars[j].name, willChangeTo) == 0) {
                    symbolTable.vars[i].address = symbolTable.vars[j].address;
                    flag = 1;
                    break;
                }
            }
            if (flag == 1) {
                break;
            }
        }
    }
    sendResponse(client_socket, "");
}
}

  // This instruction is executed when a client asks for changes in the symbol
  else if (strcmp("changes?", instruction) == 0) {
    char response[BUFFER_SIZE];
    int socketToBeUpdated = -1;
    response[0] = '\0';

    for (int i = 0; i < symbolTable.size; i++) {
      if (symbolTable.vars[i].socketToBeUpdated == client_socket) {
        socketToBeUpdated = client_socket;
        snprintf(response + strlen(response), BUFFER_SIZE - strlen(response), "%s %c ", symbolTable.vars[i].name, symbolTable.vars[i].operation);
        symbolTable.vars[i].socketToBeUpdated = -1;
      }
    }

    if (socketToBeUpdated != -1) {
      sendResponse(socketToBeUpdated, response);
    } else {
      sendResponse(client_socket, "");
    }
  }

}

// Structure to hold arguments for handleClient function
struct ThreadArgs {
  int client_socket;
  int server_socket;
};

// Define a function that each thread will execute
void * handleClient(void * arg) {
  struct ThreadArgs * args = (struct ThreadArgs * ) arg;
  int client_socket = args -> client_socket;
  int server_socket = args -> server_socket;
  char buffer[BUFFER_SIZE];

  while (1) {

    // Read data from the client
    memset(buffer, 0, sizeof(buffer));
    if (read(client_socket, buffer, BUFFER_SIZE) < 0) {
      perror("Reading from client failed");
    } else {

      // Lock the semaphore
      if (sem_wait( & semaphore) == -1) {
        perror("sem_wait failed");
        exit(1);
      }

      if (strcmp("halt", buffer) == 0) {
        halt_count++;
        // Unlock the semaphore
        if (sem_post( & semaphore) == -1) {
          perror("sem_post failed");
          exit(1);
        }
        break;
      }

      executeInstruction(buffer, client_socket);

      // Unlock the semaphore
      if (sem_post( & semaphore) == -1) {
        perror("sem_post failed");
        exit(1);
      }

    }

  }

  if (halt_count == 1 && client_sockets_count == 3) {
    // Destroy the semaphore when you're done
    if (sem_destroy( & semaphore) == -1) {
      perror("Semaphore destruction failed");
      exit(1);
    }
    exit(0);
  }

  printf("Client disconnected: %d\n", client_socket);
  close(client_socket);
  pthread_exit(NULL);
}

// This will be executed only when 1 file is received and the alarm of 10 seconds is executed
void signal_handler(int signo) {
  if (signo == SIGALRM) {
    printf("Proceeding with 1 file.\n");
    if (client_sockets_count > 0) {
      pthread_t client_thread;
      if (pthread_create( & client_thread, NULL, handleClient, & client_sockets[0]) != 0) {
        perror("Thread creation failed");
      }
      client_sockets_count += 2;
      pthread_join(client_thread, NULL);

    }
  }
}

int main() {

  // Initialize the semaphore
  if (sem_init( & semaphore, 0, 1) == -1) {
    perror("Semaphore initialization failed");
    exit(1);
  }

  // Register a signal handler for the timeout for file receiving
  signal(SIGALRM, signal_handler);
  int server_socket;
  struct sockaddr_in server_address, client_address;
  char buffer[BUFFER_SIZE];

  // Create socket
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    perror("Socket creation failed");
    exit(1);
  }

  // Set SO_REUSEADDR option
  int enable = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, & enable, sizeof(int)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    exit(1);
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(PORT);
  server_address.sin_addr.s_addr = INADDR_ANY;

  // Bind socket
  if (bind(server_socket, (struct sockaddr * ) & server_address, sizeof(server_address)) == -1) {
    perror("Socket bind failed");
    exit(1);
  }

  // Listen for incoming connections
  if (listen(server_socket, 5) == -1) {
    perror("Socket listen failed");
    exit(1);
  }

  printf("Memory Bus Server is listening on port %d...\n", PORT);

  fd_set readfds;
  struct timeval timeout;

  while (1) {

    pthread_t client_threads[MAX_CLIENTS];

    if (halt_count == client_sockets_count && client_sockets_count > 0) {
      break;
    }

    // Accept connection requests from client
    if (client_sockets_count < MAX_CLIENTS) {
      socklen_t client_len = sizeof(client_address);
      client_sockets[client_sockets_count] = accept(server_socket, (struct sockaddr * ) & client_address, & client_len);
      if (client_sockets[client_sockets_count] == -1) {
        perror("Accepting client connection failed");
        continue;
      }
      printf("Client connected: %d\n", client_sockets[client_sockets_count]);
      client_sockets_count++;

      if (client_sockets_count == 1) {
        // Set a 10-second timer after receiving the first request
        alarm(10);
      }

      printf("Number of client sockets connected: %d\n", client_sockets_count);
    }

    // If 2 clients are connected successfuly, create 2 threads respectively for both of them
    if (client_sockets_count == MAX_CLIENTS) {
      // Cancel the timeout since we have two requests
      alarm(0);
      printf("Proceeding with 2 files.\n");
      for (int i = 0; i < MAX_CLIENTS; i++) {
        struct ThreadArgs * args = (struct ThreadArgs * ) malloc(sizeof(struct ThreadArgs));
        args -> client_socket = client_sockets[i];
        args -> server_socket = server_socket;
        // Create a thread to handle this client
        if (pthread_create( & client_threads[i], NULL, handleClient, args) != 0) {
          perror("Thread creation failed");
        }
      }

      // Now, wait for each thread to finish
      for (int i = 0; i < MAX_CLIENTS; i++) {
        (pthread_join(client_threads[i], NULL));
      }
    }
  }

  // Destroy the semaphore when you're done
  if (sem_destroy( & semaphore) == -1) {
    perror("Semaphore destruction failed");
    exit(1);
  }

  close(server_socket);
  return 0;
}
