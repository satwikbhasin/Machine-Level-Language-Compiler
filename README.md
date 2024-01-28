# Machine-level-Language-Compiler

This project compiles & executes .abm files. The compiler supports stack-based programming.

## ABM Syntax
![ABM File Syntax 1](https://github.com/satwikbhasin/Machine-Level-Language-Compiler/blob/main/Assets/ABM%20Instructions%201.png)
![ABM File Syntax 2](https://github.com/satwikbhasin/Machine-Level-Language-Compiler/blob/main/Assets/ABM%20Instructions%202.png)

### Example ABM file

![ABM File Example](https://github.com/satwikbhasin/Machine-Level-Language-Compiler/blob/main/Assets/ABM%20Example.png)


## How to run the project
1. Compile abm_simulator.c & memory_bus.c to get executables: Paste "gcc -o abm_simulator abm_simulator.c" & "gcc -o memory_bus memory_bus.c" in the terminal

2. Execute memory_bus in 1 terminal window: Paste "./memory_bus" in the terminal

3. Open 2 abm_simulator executables in separate terminal windows:
- In the first terminal, paste "./abm_simulator <FILE1NAME.abm>"
- In the second terminal, paste "./abm_simulator <FILE2NAME.abm>"
