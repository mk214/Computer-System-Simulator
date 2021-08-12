#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>

using namespace std;

// ***** Function Headers *****
void terminateProcesses(int pfds[2]);
void readFile(int mem[], string fileName);
int readMemory(int &pc, int pfds[2], int pfds2[2]);
void writeMemory(int writeData[], int &writeResult, int pfds[2], int pfds2[2]);
int processMemory(int &ir, int &pc, int &x, int &y, int &ac, int &sp, int &iCount, bool &kernelMode, bool &pendingInterrupt, int pfds[2], int pfds2[2]);
int pushStack(int &sp, bool &kernelMode, int pfds[], int pfds2[], int data);
int popStack(int &sp, bool &kernelMode, int pfds[], int pfds2[]);
int interrupt(int &pc, int &sp, int pfds[], int pfds2[], bool &kernelMode, bool timer);
bool memViolation(int address, bool &kernelMode);

int main(int argc, char** argv)
{
    if(argc <= 1) // no arguments error conditions
    {
        cout << "No arugments were provided, the program will terminate" << endl;
        exit(0);
    }
    
    // get timer value and file name
    string fileName = argv[1];
    string timerInput = argv[2];
    
    int timerVal = stoi(timerInput); // convert time value into an integer
    
    if(timerVal < 1) // invalid timer, set a default timer
        timerVal = 1000;
    
    // pfds pipe is for writing to memory
    // pfds2 pipe is for reading from memory
    int pfds[2], pfds2[2];
    int result, result2; // results of pipe function call
    int pc = 0, ac = 0, ir = 0, x = 0, y = 0; // CPU registers
    int sp = 999; // Stack pointer
    int iCount = 0; // instruction count

    // create pipes
    result = pipe(pfds);
    result2 = pipe(pfds2);
    
    // check pipe errors
    if (result == -1)
        exit(1);
    if (result2 == -1)
        exit(2);

    // create child process and check for errors
    result = fork();
    if (result == -1)
        exit(1);

    if (result == 0) // Child process
    {
        int mem[2000] = {0}; // initialize memory array with all zeros
        readFile(mem,fileName); // read file data into the memory array
        int readData[3] = {0}; // array used to read data received from CPU
        int memResult = 0; // used for writing data back to CPU through the pfds2 pipe
        
        // close the write end of pfds, since the child only reads from this pipe
        close(pfds[1]);
        // close the read end of pfds2, since the child only writes to this pipe
        close(pfds2[0]);
        
        // the loop will run until a terminate signal is received from the parent process
        // the terminate signal will be a -2 that will be read in from the pipe sent by
        // the parent process
        while(true)
        {
            read(pfds[0], readData, 3 * sizeof(int)); // Memory reads data sent by CPU
          
            if(readData[0] == -2) // terminate child process
                break;
            if(readData[0] == -1) // CPU is reading data from memory
            {
                memResult = mem[readData[1]]; // read memory
                write(pfds2[1], &memResult, sizeof(memResult)); // Memory sends result to CPU
            }
            else if(readData[0] == 1) // CPU is writing data to memory
            {
                mem[readData[1]] = readData[2]; // write memory
                write(pfds2[1], &memResult, sizeof(memResult));
            }
        }
    }
    else // Parent Process
    {
        
        bool kernelMode = false; // used for determining whether an interrupt is occuring
        bool pendingInterrupt = false;
        
        // close the read end of pfds, since the parent only writes to the this pipe
        close(pfds[0]);
        // close the write end of pfds2, since the parent only reads from this pipe
        close(pfds2[1]);
        
        // the loop will run until the processMemory() function returns -1, which
        // happens with an invalid instruction, end instruction, or invalid memory access
        while(true)
        {
            if((iCount % timerVal) == 0 && pc != 0) // timer interrupt
            {
                int temp = interrupt(pc,sp,pfds,pfds2,kernelMode,true); // timer interrupt
                
                if(temp == -1) // memory violation with interrupt
                {
                    terminateProcesses(pfds);
                    break;
                }
                
                if(temp == 2) // when interrupt is already processing an interrupt
                {
                    // process a timer interrupt when the current interrupt completes
                    pendingInterrupt = true;
                }
            }
            
            ir = readMemory(pc,pfds,pfds2); // fetch next instruction
            
            // Error conditon when child process did not read in data from
            // memory, so the memory array is empty
            if(ir == 0)
            {
                break;
            }
            
            int temp = processMemory(ir,pc,x,y,ac,sp,iCount,kernelMode,pendingInterrupt,pfds,pfds2); // process instruction
            
            if(temp == -1) // exit condition for error with processing memory
            {
                terminateProcesses(pfds); // terminate child process
                break;
            }
        }
        waitpid(-1, NULL, 0); // wait for child process to terminate, then exit parent
    }
    return 0;
}
void terminateProcesses(int pfds[2])
{
    int cpuData[3] = {0};
    cpuData[0] = -2; // terminate signal
    write(pfds[1], cpuData, 3 * sizeof(int)); // close the write end of pfds, since the child only reads from this pipe
}
void readFile(int mem[], string fileName)
{
    ifstream inputFile;
    inputFile.open(fileName); // open file
    string temp, data; // for obtaining file data
    int index = 0; // where data should be placed in memory
    
    if(!inputFile) // file error condition
    {
        cout << "The file you provided was invalid, retry the program with a valid file" << endl;
        exit(0);
    }
    
    while(getline(inputFile, temp))
    {
        if(temp.empty()) // keep reading in data if the line is empty
            continue;
        
        // check if the beginning of a line is blank, if it is then ignore that line
        int num = temp.front();
        if(num == 32)
            continue;
        
        data = temp.substr(0,temp.find(" ")); // get the value before the comments
        
        // if the data obtained starts with a period, then change the index to load
        // in data the new address
        if(data[0] == 46)
        {
            data = temp.substr(1,temp.find(" ")); // remove period from address
            index = stoi(data);
            continue; // continue reading in file
        }
        
        mem[index] = stoi(data); // store data into memory
        index++;
    }
    
    inputFile.close();
}
 int readMemory(int &pc, int pfds[2], int pfds2[2])
 {
    int memResult = 0; // result that memory returns from a CPU read
    int cpuData[3] = {0};
    cpuData[0] = -1; // CPU read
    cpuData[1] = pc;
     
    write(pfds[1], cpuData, 3 * sizeof(int)); // CPU sends address of PC to Memory
    read(pfds2[0], &memResult, sizeof(memResult)); // CPU reads memory result
     
    return memResult;
 }
void writeMemory(int writeData[], int &writeResult, int pfds[2], int pfds2[2])
{
    writeData[0] = 1; // update first element for memory write condition
    write(pfds[1], writeData, 3 * sizeof(int));
    read(pfds2[0], &writeResult, sizeof(writeResult));
}
// processMemory() gets all CPU registers, the two pipes, and the instruction count
int processMemory(int &ir, int &pc, int &x, int &y, int &ac, int &sp, int &iCount, bool &kernelMode, bool &pendingInterrupt, int pfds[2], int pfds2[2])
 {
     // Switch statement will process all instructions
     switch(ir)
     {
         case 1: // load value into AC
         {
             pc++;
             int temp = readMemory(pc,pfds,pfds2); // get the value to load
             ac = temp;
             pc++;
         } break;
         case 2: // load value at the specified address
         {
             pc++;
             int address = readMemory(pc,pfds,pfds2); // get the address to be used for load
             
             if(memViolation(address,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             int data = readMemory(address,pfds,pfds2); // get data at address
             ac = data;
             pc++;
         } break;
         case 3: // Load address at the given address then get value at that address
         {
             pc++;
             int address = readMemory(pc,pfds,pfds2);
             int nextAddress = readMemory(address,pfds,pfds2);
             
             if(memViolation(address,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             else if(memViolation(address,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             int data = readMemory(nextAddress,pfds,pfds2);
             ac = data;
             pc++;
         } break;
         case 4: // Load at address + x
         {
             pc++;
             int address = readMemory(pc,pfds,pfds2);
             int addressResult = x + address;
             
             if(memViolation(addressResult,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             ac = readMemory(addressResult,pfds,pfds2);
             pc++;
         } break;
         case 5: // Load at address + y
         {
             pc++;
             int address = readMemory(pc,pfds,pfds2);
             int addressResult = y + address;
             
             if(memViolation(addressResult,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             ac = readMemory(addressResult,pfds,pfds2);
             pc++;
         } break;
         case 6: // Load at stack pointer + x
         {
             int address = x + sp;
             
             if(memViolation(address,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             ac = readMemory(address,pfds,pfds2);
             pc++;
         } break;
         case 7: // Write data in AC to memory at specified address
         {
             pc++;
             int address = readMemory(pc,pfds,pfds2); // address to write to
             
             if(memViolation(address,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             int writeData[3] = {0}; // get data ready for write
             writeData[1] = address;
             writeData[2] = ac;
             
             writeMemory(writeData,address,pfds,pfds2);
             pc++;
            } break;
         case 8: // Load random int between 1 and 100 into the AC
         {
             int random;
             srand(time(NULL));
             random = (rand() % 100) + 1;
             ac = random;
             pc++;
         } break;
         case 9: // Format for writing data
         {
             pc++;
             int temp = readMemory(pc,pfds,pfds2); // get port
             if(temp == 1) // write ac as an integer
                 cout << ac;
             else if(temp == 2) // write ac as a character
             {
                 char printAC = ac;
                 cout << printAC;
             }
             else // Invalid port, instruction skipped
             {
                 cout << "The port you provided was invalid" << endl;
             }
             pc++;
         } break;
         case 10: // add x
         {
             ac += x;
             pc++;
         } break;
         case 11: // add y
         {
             ac += y;
             pc++;
         } break;
         case 12: // sub x
         {
             ac -= x;
             pc++;
         } break;
         case 13: // sub y
         {
             ac -= y;
             pc++;
         } break;
         case 14: // CopyToX
         {
             x = ac;
             pc++;
         } break;
         case 15: // CopyFromX
         {
             ac = x;
             pc++;
         } break;
         case 16: // CopyToY
         {
             y = ac;
             pc++;
         } break;
         case 17: // CopyFromY
         {
             ac = y;
             pc++;
         } break;
         case 18: // CopyToSP
         {
             sp = ac;
             pc++;
         } break;
         case 19: // CopyFromSP
         {
             ac = sp;
             pc++;
         } break;
         case 20: // Jump to provided address
         {
             pc++;
             int temp = readMemory(pc,pfds,pfds2);
             
             if(memViolation(temp,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             pc = temp;
         } break;
         case 21: // Jump if AC is equal to 0
         {
             pc++;
             int temp = readMemory(pc,pfds,pfds2);
             
             if(memViolation(temp,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             if(ac == 0)
                pc = temp;
             else
                pc++;
         } break;
         case 22: // Jump if AC is not equal to 0
         {
             pc++;
             int temp = readMemory(pc,pfds,pfds2);
             
             if(memViolation(temp,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             if(ac != 0)
                 pc = temp;
             else
                pc++;
         } break;
         case 23: // Push return address to stack, then jump to the provided address
         {
             pc++;
             int jumpAddress = readMemory(pc,pfds,pfds2);
             
             if(memViolation(jumpAddress,kernelMode))
             {
                 iCount += 1;
                 return -1;
             }
             
             int returnAddress = pc + 1; // to move onto next instruction
             int result = pushStack(sp,kernelMode,pfds,pfds2,returnAddress);
             
             if(result == -1) // memory violation with push onto stack
             {
                 iCount += 1;
                 return -1;
             }
             
             pc = jumpAddress;
         } break;
         case 24: // Pop return address from stack and jump to the return address
         {
             int returnAddress = popStack(sp,kernelMode,pfds,pfds2); // memory violation with pop onto stack
             
             if(returnAddress == -1) // memory violation with pop from stack
             {
                 iCount += 1;
                 return -1;
             }
             
             pc = returnAddress;
         } break;
         case 25: // Increment X
         {
             x++;
             pc++;
         } break;
         case 26: // Decrement X
         {
             x--;
             pc++;
         } break;
         case 27: // Push AC onto the Stack
         {
             int result = pushStack(sp,kernelMode,pfds,pfds2,ac);
             
             if(result == -1) // memory violation with push onto stack
             {
                 iCount += 1;
                 return -1;
             }
             
             pc++;
         } break;
         case 28: // Pop AC from the Stack
         {
             ac = popStack(sp,kernelMode,pfds,pfds2);
             
             if(ac == -1) // memory violation with pop from stack
             {
                 iCount += 1;
                 return -1;
             }
             
             pc++;
         } break;
         case 29: // Interrupt
         {
             int result = interrupt(pc,sp,pfds,pfds2,kernelMode,false); // interrupt not from timer
             if(result == -1) // memory violation with interrupt
             {
                 iCount += 1;
                 return -1;
             }
         } break;
         case 30:
         {
             pc = popStack(sp,kernelMode,pfds,pfds2); // get previous pc
             sp = popStack(sp,kernelMode,pfds,pfds2); // get previous sp
             
             if(pc == -1 || sp == -1) // memory violation with pop from stack
             {
                 iCount += 1;
                 return -1;
             }
             
             kernelMode = false; // program back in user mode
             
             // when a timer interrupt was triggered during an interrupt that was being
             // processed, upon return of the current interrupt trigger the
             // timer interrupt
             if(pendingInterrupt == true)
             {
                 // set a timer interrupt
                 int temp = interrupt(pc,sp,pfds,pfds2,kernelMode,true);
                 
                 if(temp == -1) // memory violation
                 {
                     iCount += 1;
                     return -1;
                 }
                 
                 pendingInterrupt = false; // no longer have a pending interrupt
             }
         } break;
         case 50:
         {
             iCount += 1;
             return -1; // terminate condition
         } break;
         default: // Invalid instruction
         {
             cout << "You did not provide a valid instruction" << endl;
             return -1; // terminate condition
         } break;
        }
     iCount += 1; // increment the instruction count for each instruction
     return 1; // instruction was valid and processed successfully
 }
int pushStack(int &sp, bool &kernelMode, int pfds[], int pfds2[], int data)
{
    if(kernelMode && sp < 1000) // check memory violation
    {
        cout << "Memory violation: accessing system address " << sp << " in kernel mode" << endl;
        return -1;
    }
    if(kernelMode == false && sp < 0) // check memory violation
    {
        cout << "Memory violation: accessing system address " << sp << " in user mode" << endl;
        return -1;
    }
    
    sp--; // update stack pointer
    
    // update write data
    int writeData[3];
    int writeResult;
    writeData[1] = sp;
    writeData[2] = data;
    
    // write to memory
    writeMemory(writeData,writeResult,pfds,pfds2);
    
    return 1;
}
int popStack(int &sp, bool &kernelMode, int pfds[], int pfds2[])
{
    if(kernelMode && sp > 1999) // check memory violation
    {
        cout << "Memory violation: accessing system address " << sp << " in kernel mode" << endl;
        return -1;
    }
    if(kernelMode == false && sp > 999) // check memory violation
    {
        cout << "Memory violation: accessing system address " << sp << " in user mode" << endl;
        return -1;
    }
    
    int data;
    int writeData[3]; // data to be sent to memory
    int writeResult;
    
    writeData[1] = sp;
    writeData[2] = 0; // for "clearing" data
    
    data = readMemory(sp,pfds,pfds2); // pop stack
    writeMemory(writeData,writeResult,pfds,pfds2); // "clear" data by writing 0
    sp++;
    return data;
}
int interrupt(int &pc, int &sp, int pfds[], int pfds2[], bool &kernelMode, bool timer)
{
    // Timer variable will be used to determine if the interrupt is a timer interrupt
    // or if the interrupt is a syscall interrupt
    if(kernelMode) // if an interrupt is already being processed, don't process another interrupt
        return 2;
    kernelMode = true; // interrupt is now occuring
    
    // save value of old SP and update SP for kernel mode
    int oldSP = sp;
    sp = 1999;
    int result = pushStack(sp,kernelMode,pfds,pfds2,oldSP);
    if(result == -1)
        return -1;

    // if a timer interrupt, save pc to go back to the instruction that was interrupted
    // and set pc to 1000
    if(timer)
    {
        result = pushStack(sp,kernelMode,pfds,pfds2,pc);
        if(result == -1)
            return -1;
        pc = 1000;
    }
    // if a syscall interrupt, save pc + 1 to go back to next instruction
    // after syscall instruction and set pc to 1500
    else
    {
        result = pushStack(sp,kernelMode,pfds,pfds2,pc+1);
        if(result == -1)
            return -1;
        pc = 1500;
    }
    return 1;
}
bool memViolation(int address, bool &kernelMode)
{
    // check for memory violations for the address in memory
    // return true if a memory violation has occured
    // return false if the memory location is valid
    
    if(address < 0 || address > 1999)
    {
        if(kernelMode)
            cout << "Memory violation: accessing system address " << address << " in kernel mode" << endl;
        else
            cout << "Memory violation: accessing system address " << address << " in user mode" << endl;
        return true;
    }
    if(address > 999 && kernelMode == false)
    {
        cout << "Memory violation: accessing system address " << address << " in user mode" << endl;
        return true;
    }
    if(address < 999 && kernelMode == true)
    {
        cout << "Memory violation: accessing system address " << address << " in kernel mode" << endl;
        return true;
    }
    else
        return false;
}
