#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>

#define MEMORY_BUS_SERVER_IP "127.0.0.1" // Update this with the IP of your memory bus server
#define MEMORY_BUS_SERVER_PORT 7777 // Update this with the port your memory bus server is listening on
#define BUFFER_SIZE 1024

int client_socket = -1; // Global socket for reusing

// Function to create the socket and connect to the memory bus server
int connectToMemoryBusServer() {
  struct sockaddr_in server_address;

  // Create socket
  client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket == -1) {
    perror("Socket creation failed");
    return -1;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(MEMORY_BUS_SERVER_PORT);
  server_address.sin_addr.s_addr = inet_addr(MEMORY_BUS_SERVER_IP);

  // Connect to the memory bus server
  if (connect(client_socket, (struct sockaddr * ) & server_address, sizeof(server_address)) == -1) {
    perror("Connection to memory bus server failed");
    printf("ABM file has global variables but the memory bus server is not active, exiting!\n");
    exit(0);
    return -1;
  }

  return 0;
}

// Function to send a message to the memory bus server
int sendMessageToMemoryBus(const char * message) {
  if (client_socket == -1) {
    // If the socket is not connected, establish the connection
    if (connectToMemoryBusServer() != 0) {
      return -1;
    }
  }

  char buffer[BUFFER_SIZE];

  // Send the message to the memory bus server
  if (write(client_socket, message, BUFFER_SIZE) < 0) {
    perror("Sending message to memory bus server failed");
    return -1;
  }

  // Read the response from the memory bus server into the buffer
  ssize_t bytes_received = read(client_socket, buffer, BUFFER_SIZE);
  if (bytes_received < 0) {
    perror("Reading response from memory bus server failed");
    return -1;
  }

  // Convert the received string to an integer
  buffer[bytes_received] = '\0'; // Null-terminate the received string
  int response_value = atoi(buffer); // Convert to an integer

  return response_value;
}

#define CACHE_SIZE 3

// Structure for Cache Variables that stores variable's name, value, index(precedence) and state
struct CacheMemory {
  char name[50];
  int value;
  int index;
  char state;
};

// Array to store 3 cache variables
struct CacheMemory cacheMemory[CACHE_SIZE];
int cacheVariablesCount = 0;

// Checks if the given variable exists in the cache
// returns index in cache if it exists otherwise returns -1
int isVariableInCache(char varName[]) {
  for (int i = 0; i < cacheVariablesCount; i++) {
    if (strcmp(cacheMemory[i].name, varName) == 0) {
      return i;
    }
  }
  return -1;
}

// This method takes a variable name, a value and state to add to the cache by checking if
// (1) The variable is in cache then change the details of the variable and change the indexes(modified variable becomes 3
// if it is 1 or 2 and the rest demote.
// (2) The variable is not in cache then check if
//    (a) The cache is full then remove the variable indexed 1 and write it to bus and add the given variable with index 3
//        and demote the rest.
//    (b) The cache is not full, then simply add the given variable with the last available index.
void addToCache(char * varName, int value, char state) {
  if (isVariableInCache(varName) != -1) {
    if (cacheVariablesCount == CACHE_SIZE) {
      if (cacheMemory[isVariableInCache(varName)].index == 2) {
        for (int i = 0; i < CACHE_SIZE; i++) {
          if (cacheMemory[i].index == 3) {
            cacheMemory[i].index = 2;
            cacheMemory[isVariableInCache(varName)].index = 3;
          }
        }
      } else if (cacheMemory[isVariableInCache(varName)].index == 1) {
        for (int i = 0; i < CACHE_SIZE; i++) {
          if (cacheMemory[i].index == 3) {
            cacheMemory[i].index = 2;
          } else if (cacheMemory[i].index == 2) {
            cacheMemory[i].index = 1;
          }
          cacheMemory[isVariableInCache(varName)].index = 3;
        }
      }
      strcpy(cacheMemory[isVariableInCache(varName)].name, varName);
      cacheMemory[isVariableInCache(varName)].value = value;
      cacheMemory[isVariableInCache(varName)].state = state;
    } else {
      strcpy(cacheMemory[isVariableInCache(varName)].name, varName);
      cacheMemory[isVariableInCache(varName)].value = value;
      cacheMemory[isVariableInCache(varName)].state = state;
    }
  } else {
    if (cacheVariablesCount == CACHE_SIZE) {
      for (int i = 0; i < CACHE_SIZE; i++) {
        if (cacheMemory[i].index == 1) {
          if (cacheMemory[i].state == 'M') {
            char message[BUFFER_SIZE];
            snprintf(message, BUFFER_SIZE, "%s %d %s", "bus_write", value, varName);
            sendMessageToMemoryBus(message);
          }
          cacheMemory[i].index = 3;
          strcpy(cacheMemory[i].name, varName);
          cacheMemory[i].value = value;
          cacheMemory[i].state = state;
        } else if (cacheMemory[i].index == 2) {
          cacheMemory[i].index = 1;
        } else if (cacheMemory[i].index == 3) {
          cacheMemory[i].index = 2;
        }
      }
    } else {
      strcpy(cacheMemory[cacheVariablesCount].name, varName);
      cacheMemory[cacheVariablesCount].value = value;
      cacheMemory[cacheVariablesCount].state = state;
      cacheMemory[cacheVariablesCount].index = cacheVariablesCount + 1;
      cacheVariablesCount++;
    }
  }
}

#define MAX_GLOBAL_VARIABLES 100

// Structure for Global Variables that stores variable's name, value and address
struct GlobalVariable {
  char name[50];
  int value;
  int address;
};

// Array to store 100 global variables
struct GlobalVariable globalVariables[MAX_GLOBAL_VARIABLES];
int globalVariableCount = 0;

// Function to add a global variable
void addGlobalVariable(const char * name) {
  if (globalVariableCount < MAX_GLOBAL_VARIABLES) {
    strcpy(globalVariables[globalVariableCount].name, name);
    globalVariables[globalVariableCount].value = 0;
    //globalVariables[globalVariableCount].state = 'I';
    globalVariables[globalVariableCount].address = globalVariableCount;
    globalVariableCount++;
  } else {
    printf("Global variables array is full. Cannot add more variables.\n");
  }
}

// Checks if the given variable exists in the given symbol table
// returns index in symbol table if it exists otherwise returns -1
int isVariableGlobal(char varName[]) {
  for (int i = 0; i < globalVariableCount; i++) {
    if (strcmp(globalVariables[i].name, varName) == 0) {
      return globalVariables[i].address;
    }
  }
  return -1;
}

bool isPushedAddressGlobal = false;

// Define the updateState function
void updateState(char * varName, char operation) {
  if (operation == 'w') {
    if (cacheMemory[isVariableInCache(varName)].state == 'M' || cacheMemory[isVariableInCache(varName)].state == 'S') {
      cacheMemory[isVariableInCache(varName)].state = 'I';
    }
  } else if (operation == 'r') {
    if (cacheMemory[isVariableInCache(varName)].state == 'M') {
      cacheMemory[isVariableInCache(varName)].state = 'S';
      char message[BUFFER_SIZE];
      snprintf(message, BUFFER_SIZE, "%s %d %s", "bus_write", cacheMemory[isVariableInCache(varName)].value, varName);
      sendMessageToMemoryBus(message);
    }
  }
}

#define INSTRUCTIONS_SIZE 1000
#define STACK_SIZE 100
#define CALL_STACK_SIZE 1000

// Structure of a label that has a label and its corresponding address(basically the line number in file)
struct Label {
  char name[50];
  int address;
};

struct Label labels[100]; // Array to store 100 labels
int labelCount = 0; // Initialize labelCount to 0

// Structure of a variable that has variable's name and its corresponding value in Symbol Table
struct Variable {
  char name[50];
  int value;
  int address;
};

// Structure for Symbol Table that stores 1000 variables, table's size
// and the status(-1: beforeCall, 0: duringCall, 1: afterCall)
struct SymbolTable {
  struct Variable vars[1000];
  int size;
  int status;
};

struct SymbolTable symbolStack[1000]; // Array to store 100 symbol tables (i.e. symbol table stack)
int symbolStackTop = -1; // Top pointer of the symbol table stack

char instructions[INSTRUCTIONS_SIZE][100]; // A 2D character array to store instructions from .abm file
int IP = 0; // Pointer to the current instruction i

int stack[STACK_SIZE]; // Array that acts as the core stack
int top = -1; // Top pointer of the core stack

int callStack[CALL_STACK_SIZE]; // A 2D  int array that acts as the function calls stack
int callStackTop = -1; // Top pointer of the function calls stack

// Handles scope begin by:
// adding a new symbol table which has 0 variables and status is -1(before function call)
void handleBegin() {
  symbolStackTop++;
  symbolStack[symbolStackTop].size = 0;
  symbolStack[symbolStackTop].status = -1;
}

// Handles scope end by:
// decrementing the symbolStackTop pointer by 1 (almost like popping a scope)
void handleEnd() {
  if (symbolStackTop >= 0) {
    symbolStackTop--;
  } else {
    printf("Error: Unmatched 'end' instruction.\n");
  }
}

// Pushes current instruction pointer in the callStack
void pushCallStack(int currentAddress) {
  if (callStackTop < CALL_STACK_SIZE - 1) {
    callStackTop++;
    callStack[callStackTop] = IP;
  } else {
    printf("Call stack overflow! Cannot push more function calls.\n");
  }
}

// Returns the top instruction pointer from the callStack
int popCallStack() {
  if (callStackTop >= 0) {
    int topEntry = callStack[callStackTop];
    callStackTop--;
    return topEntry;
  } else {
    return -1;
  }
}

// Pushes the given value onto the core stack
void push(int value) {
  if (top < STACK_SIZE - 1) {
    top++;
    stack[top] = value;
  } else {
    printf("Stack overflow! Cannot push %d\n", value);
  }
}

// Pops the top value from the core stack
int pop() {
  if (top >= 0) {
    int value = stack[top];
    top--;
    return value;
  } else {
    return -1;
  }
}

// Checks if the given variable exists in the given symbol table
// returns index in symbol table if it exists otherwise returns -1
int isVariableInSymbolTable(char varName[], struct SymbolTable symbolTable) {
  for (int i = 0; i < symbolTable.size; i++) {
    if (strcmp(symbolTable.vars[i].name, varName) == 0) {
      return symbolTable.vars[i].address;
    }
  }
  return -1;
}

// Adds the given variable to the given symbol table and sets its initial value to 0
void addVariableToSymbolTable(char * varName, int symbolStackPointer,
  int symbolTableSize) {
  strcpy(symbolStack[symbolStackPointer].vars[symbolTableSize].name, varName);
  symbolStack[symbolStackPointer].vars[symbolTableSize].value = 0;
  symbolStack[symbolStackPointer].vars[symbolTableSize].address = symbolTableSize;
}

// Performs the rvalue operation on the given variable name in the symbol table given
void performRvalue(char * varName, int symbolStackPointer) {
  struct SymbolTable symbolTable = symbolStack[symbolStackPointer];

  int indexToUpdate = isVariableInSymbolTable(varName, symbolTable);

  if (indexToUpdate != -1) {
    push(symbolTable.vars[indexToUpdate].value);
  } else {
    addVariableToSymbolTable(varName, symbolStackPointer, symbolTable.size);
    push(symbolStack[symbolStackPointer].vars[symbolTable.size].value);
    symbolStack[symbolStackPointer].size++;
  }
}

// Performs the lvalue operation on the given variable name in the symbol table given
void performLvalue(char * varName, int symbolStackPointer) {
  struct SymbolTable symbolTable = symbolStack[symbolStackPointer];

  int indexToUpdate = isVariableInSymbolTable(varName, symbolTable);

  if (indexToUpdate != -1) {
    push(indexToUpdate);
  } else {
    addVariableToSymbolTable(varName, symbolStackPointer, symbolTable.size);
    push(symbolTable.size);
    symbolStack[symbolStackPointer].size++;
  }
}

// Takes a label name and returns it address
int findLabelAddress(const char * labelName) {
  for (int i = 0; i < labelCount; i++) {
    if (strcmp(labels[i].name, labelName) == 0) {
      return labels[i].address;
    }
  }
  return -1; // Label not found
}

// Takes an instruction and executes it if it is in the known instructions
void executeInstruction(char * instruction) {

  char stringValue[90];
  char varName[50];
  int value;

  if (strncmp(instruction, "halt", 4) == 0) {
    if (client_socket != -1) {
      sendMessageToMemoryBus("halt");
    }
    exit(0); // If instruction is "halt", the code terminates
  }
  if (sscanf(instruction, " push %d", & value) == 1) {
    push(value); // If instruction is "push x", executes the push method
  } else if (strstr(instruction, "pop") != NULL) {
    pop(); // If instruction is "pop", executes the pop method
  } else if (strstr(instruction, "show") != NULL) {
    printf("%s", strstr(instruction, "show") + 5); // If instruction is "show", prints the given string/value
  } else if (strstr(instruction, "print") != NULL) {
    if (top >= 0) {
      printf("%d\n", stack[top]); // If instruction is "print", prints the top element of the core stack
    }
  } else if (strstr(instruction, "+") != NULL) {
    int value1 = pop(); // If instruction is "+", pops the two elements from the core stack,
    int value2 = pop(); // adds them and then pushes the result back to stack
    int result = value2 + value1;
    push(result);
  } else if (strstr(instruction, "-") != NULL) {
    int value1 = pop(); // If instruction is "-", pops the two elements from the core stack,
    int value2 = pop(); // subtracts them and then pushes the result back to stack
    int result = value2 - value1;
    push(result);
  } else if (strstr(instruction, "div") != NULL) {
    int value1 = pop(); // If instruction is "div", pops the two elements from the core stack,
    int value2 = pop(); // performs the remainder operation(%) on them and then pushes the
    int result = value2 % value1; // result back to stack
    push(result);
  } else if (strstr(instruction, "/") != NULL) {
    int value1 = pop(); // If instruction is "/", pops the two elements from the core stack,
    int value2 = pop(); // divides them and then pushes the result back to stack
    int result = value2 / value1;
    push(result);
  } else if (strncmp(instruction, "&", 1) == 0) {
    int value1 = pop(); // If instruction is "&", pops the two elements from the core stack,
    int value2 = pop(); // performs logical AND operation on them and then pushes the result
    int result = 0; // back to stack
    if (value1 != 0 && value2 != 0) {
      result = 1;
    }
    push(result);
  } else if (strstr(instruction, "|") != NULL) {
    int value1 = pop(); // If instruction is "|", pops the two elements from the core stack,
    int value2 = pop(); // performs logical OR operation on them and then pushes the result
    int result = 0; // back to stack
    if (value1 != 0 || value2 != 0) {
      result = 1;
    }
    push(result);
  } else if (strstr(instruction, "!") != NULL) {
    int value = pop(); // If instruction is "!", pops the top element from the core stack,
    int result = 0; // performs logical NOT operation on it and then pushes the result
    if (value == 0) result = 1; // back to stack
    push(result);
  } else if (strstr(instruction, "<>") != NULL) {
    int value1 = pop(); // If instruction is "<>", pops the top two elements from the core stack,
    int value2 = pop(); // if they are not equal, pushes 1 to the stack otherwise pushes 0
    int result = 0;
    if (value1 != value2) result = 1;
    push(result);
  } else if (strstr(instruction, "<=") != NULL) {
    int value1 = pop(); // If instruction is "<=", pops the top two elements from the core stack,
    int value2 = pop(); // performs <= operation on them and pushes 1 if true, otherwise pushes 0
    int result = 0;
    if (value2 <= value1) result = 1;
    push(result);
  } else if (strstr(instruction, ">=") != NULL) {
    int value1 = pop(); // If instruction is ">=", pops the top two elements from the core stack,
    int value2 = pop(); // performs >= operation on them and pushes 1 if true, otherwise pushes 0
    int result = 0;
    if (value2 >= value1) result = 1;
    push(result);
  } else if (strstr(instruction, "<") != NULL) {
    int value1 = pop(); // If instruction is "<", pops the top two elements from the core stack,
    int value2 = pop(); // performs < operation on them and pushes 1 if true, otherwise pushes 0
    int result = 0;
    if (value2 < value1) result = 1;
    push(result);
  } else if (strstr(instruction, ">") != NULL) {
    int value1 = pop(); // If instruction is ">", pops the top two elements from the core stack,
    int value2 = pop(); // performs < operation on them and pushes 1 if true, otherwise pushes 0
    int result = 0;
    if (value2 > value1) result = 1;
    push(result);
  } else if (strstr(instruction, "*") != NULL) {
    int value1 = pop(); // If instruction is "*", pops the top two elements from the core stack,
    int value2 = pop(); // multiplies them and pushes the result to the core stack
    int result = value2 * value1;
    push(result);
  } else if (sscanf(instruction, " goto %s\n", stringValue) == 1 || // If Instruction is "goto x", it calls the findLabelAddress() method
    sscanf(instruction, " goto %d\n", & value) == 1) { // and sets the current instruction pointer to the address of label "x"
    int labelAddress = findLabelAddress(stringValue);
    if (labelAddress != -1) {
      IP = labelAddress;
    } else {
      printf("Label '%s' not found.\n", stringValue);
    }
  } else if (sscanf(instruction, " gofalse %s\n", stringValue) == 1 || // If Instruction is "gofalse x", checks the top element of the stack,
    sscanf(instruction, " goto %d\n", & value) == 1) { // if its false(0), it performs the goto operation
    if (pop() == 0) {
      int labelAddress = findLabelAddress(stringValue);
      if (labelAddress != -1) {
        IP = labelAddress;
      } else {
        printf("Label '%s' not found.\n", stringValue);
      }
    }
  } else if (sscanf(instruction, " gotrue %s\n", stringValue) == 1 || // If Instruction is "gofalse x", checks the top element of the stack,
    sscanf(instruction, " gotrue %d\n", & value) == 1) { // if its true(1), it performs the goto operation
    if (pop() != 0) {
      char * labelName = strtok(
        instruction + 7,
        " \t\n\r");
      int labelAddress = findLabelAddress(labelName);
      if (labelAddress != -1) {
        IP = labelAddress;
      } else {
        printf("Label '%s' not found.\n", labelName);
      }
    }
  } else if (strncmp(instruction, "copy", 4) == 0) { // If Instruction is "copy", pushes the top value of the stack to the stack
    push(stack[top]);
  } else if (sscanf(instruction, " rvalue %s", varName) == 1) { // If Instruction is "rvalue x", checks if the variable is global, if it is
    if (isVariableGlobal(varName) != -1) {			// then checks if it is in the cache, if it is and the state in cache is I,
      if (isVariableInCache(varName) != -1) {			// then reads from the bus and adds to cache & local copy of global memory
        if (cacheMemory[isVariableInCache(varName)].state == 'I') {
          char message[1024];
          snprintf(message, sizeof(message), "%s %s", "bus_read", varName);
          int value = -1;
          value = sendMessageToMemoryBus(message);
          push(value);
          addToCache(varName, value, 'S');
          globalVariables[isVariableGlobal(varName)].value = value;
        } else if (cacheMemory[isVariableInCache(varName)].state == 'S' || cacheMemory[isVariableInCache(varName)].state == 'M') {
          int value = cacheMemory[isVariableInCache(varName)].value; // If variable is in cache and it's state is S or M, then just push it's value
          push(value);						     // to stack
        }
      } else {							// If it is not in cache, then read the value from the bus and add it to the cache
        char message[1024];					// and set it's state to S
        snprintf(message, sizeof(message), "%s %s", "bus_read", varName);
        int value = -1;
        value = sendMessageToMemoryBus(message);
        push(value);
        addToCache(varName, value, 'S');
        globalVariables[isVariableGlobal(varName)].value = value;
      }
    } else
    if (symbolStack[symbolStackTop].status == -1 && symbolStackTop != 0) {   // If the variable is not global, checks the status of the current
      performRvalue(varName, symbolStackTop - 1);     // symbol table, if it is beforeCall and the symbolStackTop pointer
    } else if (symbolStack[symbolStackTop].status == 1 || // is not 0(i.e. its not the main scope), performs rvalue on the
      symbolStack[symbolStackTop].status == 0 || // symbol table below the top i.e the previous scope otherwise
      symbolStackTop == 0) { // performs rvalue on the current symbol table i.e. the current scope
      performRvalue(varName, symbolStackTop);
    }
  } else if (sscanf(instruction, " lvalue %s", varName) == 1) { // If Instruction is "lvalue x", checks if the variable is global, if it is
    if (isVariableGlobal(varName) != -1) {			// then simply pushes its address from local copy of global variables to the stack

      isPushedAddressGlobal = true;

      for (int i = 0; i < globalVariableCount; i++) {
        if (strcmp(globalVariables[i].name, varName) == 0) {
          push(globalVariables[i].address);
          break;
        }
      }
    } else {
      isPushedAddressGlobal = false;
      if (symbolStack[symbolStackTop].status == -1 || // If it is not global, then checks its status of symbol table, if it is beforeCall or
        symbolStack[symbolStackTop].status == 0 || symbolStackTop == 0) { // duringCall or symbolStackTop pointer is  0(i.e. its the main scope),
        performLvalue(varName, symbolStackTop); // performs lvalue on the current symbol table i.e the current scope otherwise
      } else if (symbolStack[symbolStackTop].status == 1) { // performs lvalue on the symbol table below the top i.e. the
        performLvalue(varName, symbolStackTop - 1); // previous scope
      }
    }
  } else if (strstr(instruction, ":=") != NULL) {
    int value = pop();
    int address = pop(); 					// If Instruction is ":=", then checks if the boolean "isPushedAddressGlobal" is
    if (isPushedAddressGlobal) {    				// true or false. If it is global then, changes value of all variables which
      for (int i = 0; i < globalVariableCount; i++) {         	// have the address as the given address, If the variable state is I, then write
        if (globalVariables[i].address == address) {            // the given variable to bus and addToCache() and update value in global variables.
          if (isVariableInCache(globalVariables[i].name) != -1) {  // If the variable state is M, then just addToCache() and update value in global
            if (cacheMemory[isVariableInCache(globalVariables[i].name)].state == 'S' ||  cacheMemory[isVariableInCache(globalVariables[i].name)].state == 'I') {
              char message[BUFFER_SIZE];			// variables. If the variable is not in cache, then just write to bus and add to
              snprintf(message, BUFFER_SIZE, "%s %d %s", "bus_write", value, globalVariables[i].name); // cache and global memory.
              sendMessageToMemoryBus(message);
              addToCache(globalVariables[i].name, value, 'M');
              globalVariables[i].value = value;

            } else if (cacheMemory[isVariableInCache(globalVariables[i].name)].state == 'M') {
              addToCache(globalVariables[i].name, value, 'M');
              globalVariables[i].value = value;
            }
          } else {
            char message[BUFFER_SIZE];
            snprintf(message, BUFFER_SIZE, "%s %d %s", "bus_write", value, globalVariables[i].name);
            sendMessageToMemoryBus(message);
            globalVariables[i].value = value;
            addToCache(globalVariables[i].name, value, 'M');
          }
        }
      }
    } else { // If the variable is not global, checks the status of the current symbol table, if it is
      if (symbolStack[symbolStackTop].status == -1 || // beforeCall or duringCall, performs := on the current symbol table
        symbolStack[symbolStackTop].status == 0) { // otherwise performs := on the symbol table below the top i.e. the
        symbolStack[symbolStackTop].vars[address].value = value; // the previous scope
      } else if (symbolStack[symbolStackTop].status == 1) {
        symbolStack[symbolStackTop - 1].vars[address].value = value;
      }
    }

  } else if (strncmp(instruction, ":&", 2) == 0) {  // If Instruction is ":&", then for all the variables which have the address as address2
    int address1 = pop();			    // then replace it with address1. also change the values in cache. This is where the syncing
    int address2 = pop();		            // happens
    for (int i = 0; i < globalVariableCount; i++) {
      if (globalVariables[i].address == address2) {
        globalVariables[i].address = address1;
        if (cacheMemory[i].state == 'I' || cacheMemory[i].state == 'S') {
          cacheMemory[i].state = 'M';
        }
        char message[1024];
        snprintf(message, sizeof(message), "%s %s %s", ":&", globalVariables[i].name, globalVariables[address1].name);
        sendMessageToMemoryBus(message);
        for (int j = 0; j < globalVariableCount; j++) {
          if (globalVariables[j].address == globalVariables[i].address) {
            addToCache(globalVariables[i].name, globalVariables[globalVariables[i].address].value, 'M');
          }
        }
      }
    }
  } else if (strstr(instruction, "call") != NULL) { // If Instruction is "call x", pushes the current instruction
    char functionName[50]; // pointer to the callStack, changes the current IP to x's
    if (sscanf(instruction, " call %s", functionName) == 1) { // address(line number), sets the current symbol table's
      pushCallStack(IP); // status to duringCall
      IP = findLabelAddress(functionName);
      symbolStack[symbolStackTop].status = 0;
    }
  } else if (strstr(instruction, "return") != NULL) { // If Instruction is "return", pops the call stack to get the
    IP = popCallStack(); // line to jump to and sets the currentSymbolTables status to
    symbolStack[symbolStackTop].status = 1; // afterCall
  } else if (strstr(instruction, "begin") != NULL) {
    handleBegin(); // If Instruction is "begin", calls the handleBegin() method
  } else if (strstr(instruction, "end") != NULL) {
    handleEnd(); // If Instruction is "end", calls the handleEnd() method
  }
}

int main(int argc, char * argv[]) {
  if (argc != 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  // Opens the instructions file given in arguments in read mode
  FILE * file = fopen(argv[1], "r");
  if (file == NULL) {
    perror("Error opening file");
    return 1;
  }

  // Creates the first scope
  handleBegin();

  // Character array to store the current instruction(line) in the file
  char instruction[100];

  // Set every element of the 2D instructions array to a null character
  for (int i = 0; i < INSTRUCTIONS_SIZE; i++) {
    memset(instructions[i], '\0', sizeof(instructions[i]));
  }

  // Load the instructions into instructions array
  int address = 0;
  char integers[50];
  int array_count = 0;
  while (address < INSTRUCTIONS_SIZE &&
    fgets(instruction, sizeof(instruction), file) != NULL) {
    if (address >= INSTRUCTIONS_SIZE) {
      printf(
        "Instructions array overflow! Cannot store more "
        "instructions.\n");
    }
    // If "label x" is encountered, add it to the labels array with name as x
    // and address as the current line number in file
    if (strstr(instruction, "label") != NULL) {
      struct Label label;
      sscanf(instruction, " label %s", label.name);
      label.address = address;
      labels[labelCount++] = label;
    } else if (sscanf(instruction, " .int %99[^\n] ", integers) == 1) {
      array_count++;
      char message[1024]; // Assuming a maximum message length of 1024 characters

      // Append the string you want to add to 'integers' to 'message'
      snprintf(message, sizeof(message), "%s %s", "global_vars", integers);
      if (sendMessageToMemoryBus(message) != -1) {

        char * token = strtok(integers, " "); // Split by space
        while (token != NULL) {
          if (strcmp(token, "gloabl_vars") != 0) {
            addGlobalVariable(token);
            token = strtok(NULL, " ");
          }
        }

      }
    } else {
      strcpy(instructions[address], instruction);
    }

    address++;
  }

  fclose(file);

  // Exececute all the instructions in the 2D instructions array
  while (IP >= 0 && IP < INSTRUCTIONS_SIZE) {
    char buffer[BUFFER_SIZE];
    if (globalVariableCount > 0) {
      while (1) {
        // Before reading every instruction, asks the bus if there have been any changes
        char message[BUFFER_SIZE];
        snprintf(message, BUFFER_SIZE, "%s", "changes?");
        // Send the message to the memory bus server
        if (write(client_socket, message, BUFFER_SIZE) < 0) {
          perror("Sending message to memory bus server failed");
          return -1;
        }

        // Read the response from the memory bus server into the buffer
        size_t bytes_received = read(client_socket, buffer, BUFFER_SIZE);
        if (bytes_received < 0) {
          perror("Reading response from memory bus server failed");
          return -1;
        }

        if (bytes_received > 0) {
          // Convert the received string to an integer
          buffer[bytes_received] = '\0'; // Null-terminate the received string
          char * token;
          char varName[50];
          char operation;
          token = strtok(buffer, " ");
          while (token != NULL) {
            // Extract the variable name and operation
            snprintf(varName, sizeof(varName), "%s", token);
            token = strtok(NULL, " ");
            if (token != NULL) {
              operation = token[0]; // Assuming the operation is a single character
              // Call the updateState function to update the symbol table
              updateState(varName, operation);
            }
            token = strtok(NULL, " ");
          }
          break;
        }
      }
    }

    char * instruction = instructions[IP];
    //printf("Current instruction is [%s] and IP is: %d\n", instruction, IP);
    executeInstruction(instruction);
    IP++;
  }

  return 0;
}
