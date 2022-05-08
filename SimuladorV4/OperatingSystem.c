#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun(int);
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_HandleClockInterrupt();
int OperatingSystem_ExtractFromSleeping();
int OperatingSystem_CheckQueue();
int OperatingSystem_GetExecutingProcessID();
int OperatingSystem_CheckLTS();
void OperatingSystem_ReleaseMainMemory();

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;


// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
int initialPID=PROCESSTABLEMAXSIZE-1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 

// Array that contains the identifiers of the READY processes
//heapItem readyToRunQueue[PROCESSTABLEMAXSIZE];
//int numberOfReadyToRunProcesses=0;

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

//EX10
char * statesNames[5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"};

//EXERCISE 11
heapItem readyToRunQueue[NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};
char * queueNames [NUMBEROFQUEUES]={"USER","DAEMONS"}; 

//Ex4 V2
int numberOfClockInterrupts = 0;

// In OperatingSystem.c Exercise 5-b of V2
// Heap with blocked processes sorted by when to wakeup
heapItem sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0;

//NUMBER OF TOTAL INITIALIZED PARTITIONS
int numOfTotalInitializedPartitions;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code
	programFile=fopen("OperatingSystemCode", "r");
	if (programFile==NULL){
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing Operating System!\n");
		exit(1);		
	}

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list  all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);
	
	// V3 EX1 C
	ComputerSystem_FillInArrivalTimeQueue();

	// V3 EX1 D
	OperatingSystem_PrintStatus();	

	numOfTotalInitializedPartitions = OperatingSystem_InitializePartitionTable(); //V4 EX5

	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();

	if(numberOfNotTerminatedUserProcesses == 0 && OperatingSystem_IsThereANewProgram() == EMPTYQUEUE){ //EX 15
		// finishing sipID, change PC to address of OS HALT instruction
		OperatingSystem_TerminatingSIP();
		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");		
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}

	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);		
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// Daemon processes are system processes, that is, they work together with the OS.
// The System Idle Process uses the CPU whenever a user process is able to use it
int OperatingSystem_PrepareStudentsDaemons(int programListDaemonsBase) {

	// Prepare aditionals daemons here
	// index for aditionals daemons program in programList
	// programList[programListDaemonsBase]=(PROGRAMS_DATA *) malloc(sizeof(PROGRAMS_DATA));
	// programList[programListDaemonsBase]->executableName="studentsDaemonNameProgram";
	// programList[programListDaemonsBase]->arrivalTime=0;
	// programList[programListDaemonsBase]->type=DAEMONPROGRAM; // daemon program
	// programListDaemonsBase++

	return programListDaemonsBase;
};


// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler() {
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;
	while(OperatingSystem_IsThereANewProgram() == YES){// V3 EX3
		i = Heap_poll(arrivalTimeQueue,QUEUE_ARRIVAL,&numberOfProgramsInArrivalTimeQueue);
		PID=OperatingSystem_CreateProcess(i);
		if(PID < 0){ //CHANGE EX4
			if(PID == NOFREEENTRY){
				OperatingSystem_ShowTime(SYSPROC);
				ComputerSystem_DebugMessage(103, ERROR,programList[i]->executableName);
			}
			if(PID == PROGRAMDOESNOTEXIST){
				OperatingSystem_ShowTime(SYSPROC);
				ComputerSystem_DebugMessage(104, ERROR,programList[i]->executableName,"-it does not exist-"); //CHANGE EX5
			}
			if(PID == PROGRAMNOTVALID){
				OperatingSystem_ShowTime(SYSPROC);
				ComputerSystem_DebugMessage(104, ERROR,programList[i]->executableName,"-invalid priority or size-"); //CHANGE EX5
			}
			if(PID == TOOBIGPROCESS){
				OperatingSystem_ShowTime(SYSPROC);
				ComputerSystem_DebugMessage(105, ERROR,programList[i]->executableName); //CHANGE EX6
			}			
		}
		else{
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
			OperatingSystem_PrintStatus();		
		}
	}
	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}

int OperatingSystem_GetExecutingProcessID(){
	return executingProcessID;
}

// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	//CHANGES EX4
	if(PID < 0){
		return NOFREEENTRY;
	}

	// Check if programFile exists
	programFile=fopen(executableProgram->executableName, "r");
	if (programFile==NULL){
		return PROGRAMDOESNOTEXIST;
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(programFile);	

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);

	if(processSize < 0 || priority < 0){
		return PROGRAMNOTVALID; //CHANGE EX5
	}
	
	// Obtain enough memory space
	int selectedPartition = OperatingSystem_ObtainMainMemory(processSize, PID, indexOfExecutableProgram); //V4 EX6 C
 	

	if(selectedPartition < 0){
		if(selectedPartition == TOOBIGPROCESS){
			return TOOBIGPROCESS; //CHANGE EX6
		}			
		if(selectedPartition == MEMORYFULL){
			OperatingSystem_ShowTime(ERROR); //V4 EX6 D
			ComputerSystem_DebugMessage(144,ERROR,programList[indexOfExecutableProgram]->executableName);
			return MEMORYFULL;
		}
	}
	else{
		loadingPhysicalAddress=partitionsTable[selectedPartition].initAddress; //V4 EX6 C
		OperatingSystem_ShowTime(SYSMEM); //V4 EX6 C
		ComputerSystem_DebugMessage(143,SYSMEM,selectedPartition,loadingPhysicalAddress,partitionsTable[selectedPartition].size,
			PID, programList[indexOfExecutableProgram]->executableName);	
	}

	// Load program in the allocated memory
	int address = OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize);
	
	if(address < 0){
		return TOOBIGPROCESS; //CHANGE EX7
	}

	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);
	
	OperatingSystem_ShowPartitionTable("after allocating memory");

	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);	
	
	return PID;
		
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID, int indexOfExecutableProgram) {
	OperatingSystem_ShowTime(SYSMEM);
	ComputerSystem_DebugMessage(142,SYSMEM,PID,programList[indexOfExecutableProgram]->executableName,processSize);

	OperatingSystem_ShowPartitionTable("before allocating memory");

	int i;
	int minimumActual = 10000000;
	int minimumNext = 0;
	int iSelect = -1;
	int maxSize = 0;
	for(i = 0; i<numOfTotalInitializedPartitions; i++){
		if(partitionsTable[i].size >= maxSize){
			maxSize = partitionsTable[i].size;
		}
		if(partitionsTable[i].size >= processSize && partitionsTable[i].PID == NOPROCESS){
			minimumNext = partitionsTable[i].size;
			if(minimumActual > minimumNext){
				minimumActual = minimumNext;
				iSelect = i;
			}
		}
	}

	if(iSelect != -1){
		partitionsTable[iSelect].PID = PID;
		processTable[PID].partition = iSelect;
	}
	
 	if (processSize>maxSize){
		return TOOBIGPROCESS;
	}		

	if(iSelect == -1){
		return MEMORYFULL;
	}

 	return iSelect;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].state=NEW;	
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(111,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[0]);
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {
		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
	} 
	else {
		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
	}
	processTable[PID].queueID = programList[processPLIndex]->type;
}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {
	
	if (Heap_add(PID, readyToRunQueue[processTable[PID].queueID],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[processTable[PID].queueID] ,PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[processTable[PID].state],statesNames[1]);
		processTable[PID].state=READY;
	} 
	//OperatingSystem_PrintReadyToRunQueue();
}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess = NOPROCESS;
	int i;
	for(i = 0; i < NUMBEROFQUEUES && selectedProcess==NOPROCESS; i++){
		selectedProcess=OperatingSystem_ExtractFromReadyToRun(i);
	}
	//selectedProcess=OperatingSystem_ExtractFromReadyToRun();
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun(int queueID) {
  
	int selectedProcess=NOPROCESS;

	selectedProcess=Heap_poll(readyToRunQueue[queueID],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[queueID]);
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[processTable[PID].state],statesNames[2]);
	// Change the process' state
	processTable[PID].state=EXECUTING;
	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	
	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);

	Processor_SetAccumulator(processTable[PID].copyOfAccumulator); //EX 13
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);

	processTable[PID].copyOfAccumulator = Processor_GetAccumulator(); //EX 13
	
}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	//OperatingSystem_ShowTime(SYSPROC);
	//ComputerSystem_DebugMessage(71,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
	if(Processor_GetRegisterB() == DIVISIONBYZERO){
		OperatingSystem_ShowTime(INTERRUPT);
		ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"division by zero");
	}
	if(Processor_GetRegisterB() == INVALIDPROCESSORMODE){
		OperatingSystem_ShowTime(INTERRUPT);
		ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"invalid processor mode");
	}
	if(Processor_GetRegisterB() == INVALIDADDRESS){
		OperatingSystem_ShowTime(INTERRUPT);
		ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"invalid address");
	}
	if(Processor_GetRegisterB() == INVALIDINSTRUCTION){
		OperatingSystem_ShowTime(INTERRUPT);
		ComputerSystem_DebugMessage(140,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,"invalid instruction");
	}

	OperatingSystem_TerminateProcess();
	OperatingSystem_PrintStatus();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
  	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,
		statesNames[processTable[executingProcessID].state],statesNames[4]);

	OperatingSystem_ShowPartitionTable("before releasing memory");
	OperatingSystem_ReleaseMainMemory(); //V4 EX8
	 OperatingSystem_ShowPartitionTable("after releasing memory");

	processTable[executingProcessID].state=EXIT;
	
	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM){
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	} 	
	
	if (numberOfNotTerminatedUserProcesses==0 && OperatingSystem_IsThereANewProgram() == EMPTYQUEUE) {
		if (executingProcessID==sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			OperatingSystem_TerminatingSIP();
			OperatingSystem_ShowTime(SHUTDOWN);
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();	

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

void OperatingSystem_ReleaseMainMemory(){
		OperatingSystem_ShowTime(SYSMEM);
		ComputerSystem_DebugMessage(145,SYSMEM,processTable[executingProcessID].partition,partitionsTable[processTable[executingProcessID].partition].initAddress
			,partitionsTable[processTable[executingProcessID].partition].size,
			executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);

		partitionsTable[processTable[executingProcessID].partition].PID = NOPROCESS;
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
		case SYSCALL_YIELD:	{
			int processQueueID = processTable[executingProcessID].queueID;
			if(numberOfReadyToRunProcesses[processQueueID] > 0){		
				int currentPriority = processTable[executingProcessID].priority;				
				int newPID = Heap_getFirst(readyToRunQueue[processQueueID],numberOfReadyToRunProcesses[processQueueID]);
				//readyToRunQueue[processQueueID][0].info;

				if(currentPriority == processTable[newPID].priority){
				ComputerSystem_DebugMessage(115,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,
				newPID,programList[processTable[newPID].programListIndex]->executableName);
				int selectedPID = OperatingSystem_ShortTermScheduler();
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(selectedPID);
				}
				OperatingSystem_PrintStatus();
			}			
			break;								
		}
		case SYSCALL_SLEEP: {
			//First save the context
			OperatingSystem_SaveContext(executingProcessID);
			OperatingSystem_moveToTheBlockedState(executingProcessID);
			OperatingSystem_Dispatch(OperatingSystem_ShortTermScheduler());
			OperatingSystem_PrintStatus();
			break;
		}
		default:{
			OperatingSystem_ShowTime(INTERRUPT);
			ComputerSystem_DebugMessage(141,INTERRUPT,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,systemCallID);
			OperatingSystem_TerminateProcess();
			OperatingSystem_PrintStatus();
			break;
		}
	}
}

void OperatingSystem_moveToTheBlockedState(int PID){
	int whenToWakeUp = abs(Processor_GetAccumulator()) + numberOfClockInterrupts + 1;
	processTable[executingProcessID].whenToWakeUp = whenToWakeUp;
	if (Heap_add(PID, sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses ,PROCESSTABLEMAXSIZE)>=0) {
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,
		statesNames[processTable[executingProcessID].state],statesNames[BLOCKED]);
		processTable[executingProcessID].state=BLOCKED;
	} 
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // CLOCKINT_BIT = 9
			OperatingSystem_HandleClockInterrupt();
			break;
	}
}

// In OperatingSystem.c Exercise 2-b of V2
void OperatingSystem_HandleClockInterrupt(){
	int selectedProcess = NOPROCESS; //MODIFIED EX6 V2
	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120,INTERRUPT,numberOfClockInterrupts);//CLOCK INTERRUPT
	int actualProcesses = numberOfReadyToRunProcesses[USERPROGRAM] + numberOfReadyToRunProcesses[DAEMONPROGRAM];
	//while(numberOfSleepingProcesses != 0 && processTable[sleepingProcessesQueue[0].info].whenToWakeUp == numberOfClockInterrupts){ //WHILE THERE ARE SLEEPING PROCESSES
	int wakeup = processTable[Heap_getFirst(sleepingProcessesQueue,numberOfSleepingProcesses)].whenToWakeUp;
	while(numberOfSleepingProcesses != 0 && wakeup == numberOfClockInterrupts){ //WHILE THERE ARE SLEEPING PROCESSES //UPDATE POST CORRECTION 
		selectedProcess = OperatingSystem_ExtractFromSleeping();
		OperatingSystem_MoveToTheREADYState(selectedProcess);

	}

	OperatingSystem_LongTermScheduler(); //V3 EX4 A


	if (numberOfNotTerminatedUserProcesses == 0 && OperatingSystem_IsThereANewProgram() == EMPTYQUEUE && executingProcessID == sipID)
	{
		OperatingSystem_ReadyToShutdown();
	}

	int actualNewProcesses = numberOfReadyToRunProcesses[USERPROGRAM] + numberOfReadyToRunProcesses[DAEMONPROGRAM];

	if (actualProcesses < actualNewProcesses){ //IF THERE IS A PROCESS
		OperatingSystem_PrintStatus();

		int nextProcessPID = OperatingSystem_CheckQueue(); //Show next PID for the next program

		if(nextProcessPID != NOPROCESS){
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121, SHORTTERMSCHEDULE, executingProcessID, programList[processTable[executingProcessID].programListIndex]->executableName,
				nextProcessPID,programList[processTable[nextProcessPID].programListIndex]->executableName);

				OperatingSystem_PreemptRunningProcess(READY);
				OperatingSystem_Dispatch(nextProcessPID);

				OperatingSystem_PrintStatus();
		}
	}
	 return; 
}

int OperatingSystem_CheckQueue(){
	if(processTable[executingProcessID].queueID == USERPROCESSQUEUE){
		if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0 && processTable[executingProcessID].priority > processTable[Heap_getFirst(readyToRunQueue[USERPROCESSQUEUE],numberOfReadyToRunProcesses[USERPROCESSQUEUE])].priority){
			return OperatingSystem_ShortTermScheduler();
		}
	}
	else{
		if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] > 0){
			return OperatingSystem_ShortTermScheduler();
		}
		else{
			if(numberOfReadyToRunProcesses[DAEMONSQUEUE] > 0 && processTable[executingProcessID].priority > processTable[Heap_getFirst(readyToRunQueue[DAEMONSQUEUE],numberOfReadyToRunProcesses[DAEMONSQUEUE])].priority){
				return OperatingSystem_ShortTermScheduler();
			}
		}
	}	
	return NOPROCESS;	
}

int OperatingSystem_ExtractFromSleeping(){ //EXERCISE 6 V2
	int actualProcess = Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP, &numberOfSleepingProcesses);
	return actualProcess;
}


void OperatingSystem_PrintReadyToRunQueue(){ //EX9
	int i;
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(106,SHORTTERMSCHEDULE);
	//USER PROCESESS
	ComputerSystem_DebugMessage(112,SHORTTERMSCHEDULE, queueNames[USERPROCESSQUEUE]);
	for(i = 0 ; i<numberOfReadyToRunProcesses[USERPROCESSQUEUE] ; i++){
		if(i == numberOfReadyToRunProcesses[USERPROCESSQUEUE]-1){
			ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE,readyToRunQueue[USERPROCESSQUEUE][i].info, processTable[readyToRunQueue[USERPROCESSQUEUE][i].info].priority, "\n");
		}				
		else{
			ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE,readyToRunQueue[USERPROCESSQUEUE][i].info, processTable[readyToRunQueue[USERPROCESSQUEUE][i].info].priority, ",");
		}
	}
	if(numberOfReadyToRunProcesses[USERPROCESSQUEUE] == 0){
		ComputerSystem_DebugMessage(113,SHORTTERMSCHEDULE);
	}
	//DAEMON PROCESESS
	ComputerSystem_DebugMessage(112,SHORTTERMSCHEDULE, queueNames[DAEMONSQUEUE]);
	for(i = 0 ; i<numberOfReadyToRunProcesses[DAEMONSQUEUE] ; i++){
		if(i == numberOfReadyToRunProcesses[DAEMONSQUEUE]-1){
			ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE,readyToRunQueue[DAEMONSQUEUE][i].info, processTable[readyToRunQueue[DAEMONSQUEUE][i].info].priority, "\n");
		}				
		else{
			ComputerSystem_DebugMessage(108,SHORTTERMSCHEDULE,readyToRunQueue[DAEMONSQUEUE][i].info, processTable[readyToRunQueue[DAEMONSQUEUE][i].info].priority, ",");
		}
	}
	if(numberOfReadyToRunProcesses[DAEMONSQUEUE] == 0){
		ComputerSystem_DebugMessage(113,SHORTTERMSCHEDULE);
	}
}


