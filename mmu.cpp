//Stephanie Michalowicz
//Operating Systems
//Virtual Memory Manager

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <string.h>
#include <cstring>
#include <sstream>
#include <locale>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <unistd.h>
#include <ctype.h>
#include <bitset>


using namespace std; 

//file names
ifstream the_file;
ifstream random_file;

char line[1024];
char operation;      
int  vpage;  
unsigned long iCount = -1;
unsigned long ctx_switches;
unsigned long long cost; 
int num_frames = 32; // this will become an option 
int num_processes;
char buffer[1024] = "";
int num_rand = 0;
unsigned int *randvals;
int NRU_count = 0;

//flags
bool O_flag = false; //output
bool P_flag = false; //pagetable option
bool F_flag = false; //frame table
bool S_flag = false; //summary
bool f_flag = false; //fifo
bool s_flag = false; //second chance
bool r_flag = false; //random
bool n_flag = false; //not recently used
bool c_flag = false; //clock
bool a_flag = false; //aging


//VMA 
struct VMA {
	int start_vpage;				
	int end_vpage;
	int write_protected; 			
	int file_mapped;
};

//page table entry 
struct pte {	
	signed frameAddress:7;   
	unsigned present:1;
	unsigned modified:1; 
	unsigned writeProtected:1;
	unsigned referenced:1;
	unsigned pagedout:1; 
	unsigned fileMapped:1;
}; 

//process stats
struct stats{
	unsigned long unmaps;
	unsigned long maps;
	unsigned long ins;
	unsigned long outs;
	unsigned long fins;
	unsigned long fouts;
	unsigned long zeros;
	unsigned long segv;
	unsigned long segprot;

};

//process 
struct process {
	int num_VMAS;
	int pid;                 //process ID
	vector <VMA> vma_list;
	pte pte_table[64];  	//single level pagetable structure consisting of page table entries -- 32 bits
	stats pstats;
};

vector <process> process_table(num_processes);

process * currentProcess;

pte * pageTable = &process_table[0].pte_table[0];

void initialize(process * proc){

		//initalize the page table in each process
		for (int i = 0; i < 64; i++){
  			pte * p = &proc->pte_table[i];
            p->frameAddress = -1;
            p->present = 0;
            p->writeProtected = 0;
            p->modified = 0;
            p->referenced = 0;
            p->pagedout = 0;
            p->fileMapped = 0;
       }

       //initialize the stats for each process
       stats * s = &proc->pstats;
       s->unmaps = 0;
       s->maps = 0;
       s->ins = 0;
       s->outs = 0;
       s->fins = 0;
       s->fouts = 0;
       s->zeros = 0;
       s->segv = 0;
       s->segprot = 0;
}

//frame 
struct frame {         	 
	unsigned age; //int is size 32
	int proccess_id;
	int page_id;

	//frame constructor
	frame(){
		age = 0;
		proccess_id = -1;		 
		page_id = -1;			
	}
}; 

//frame table
vector <frame> frame_table(num_frames);  //need to support 128 frames

//assign frames from the free list
//with an index into the frame table, starting with 0
//once your run out of free frames, implement paging

static int ind = -1;

int allocate_frame_freeList(){   		
	ind++;						
	if (ind >= num_frames){
		return -1;	
	}else{
		return ind;
	}   
}

//read next line from the file
char * get_next_line(char * line){
	//returns next valid line or null
	while(the_file.getline(line, 1024)){
		if((line[0] == '#') or (line[0] == '\0') ){
			continue;
		}
		return line;
	}
	return NULL;
}


//read next line from the random file, return a random number
int myrandom(int val){

	static int ofs = 0;
	int retValue = (randvals[ofs] % val);
	ofs = (ofs + 1) % num_rand;

	return retValue;
}


//check for valid pages in the VMAS 
int page_to_vma(){

    for(int i = 0; i < currentProcess->num_VMAS; i++){
    	//return valid page
    	if((vpage >= currentProcess->vma_list[i].start_vpage) && (vpage <= currentProcess->vma_list[i].end_vpage)){
			//otherwise update the fileMapped and writeProtected bits for that pte
			pageTable[vpage].fileMapped = currentProcess->vma_list[i].file_mapped;
			pageTable[vpage].writeProtected = currentProcess->vma_list[i].write_protected;
    		return i;
    	}
    }
    return -1;
}


//pager generic parent class
class Pager{
	public:
		Pager(){};
		virtual int select_victim_frame() = 0; 
};

//Global Pager Variable
Pager * pager;

//get a frame: either from the free list or select a victim frame
int get_frame_num(){ 
   int getFrame;
   //first try to allocate a frame from the free list
   getFrame = allocate_frame_freeList();

   //otherwise select a victim frame
   if (getFrame == -1) {
       getFrame = pager->select_victim_frame();
   }
   return getFrame;
}

//read the instruction and assign op / vp, increase instruction count 
bool get_next_instruction(char * op, int * vp){    

	if(get_next_line(line)){
		//set instructions or return false 
		* op = line[0];
		* vp = atoi(&line[2]);

		//count the instructions	
		iCount++;

		//print the line
		if (O_flag){
			printf("%lu: ==> %s\n", iCount, line);
		}
		return true;
		
	}
 	return false;
}

//FIFO pager
class FIFO : public Pager{
public:
	FIFO(){
		victim_frame = 0;
	};

	int victim_frame;
	int select_victim_frame();
};

/////////////////Define FIFO functions
int FIFO :: select_victim_frame(){

	int rc = victim_frame;
	victim_frame = (victim_frame + 1) % num_frames;
	return rc;
}
/////////////////////////////////////////////

class SecondChance : public Pager{
	public:
		SecondChance(){
			victim_frame = 0;
		};

		int victim_frame;
		int select_victim_frame();							
};

////////////Define SecondChance functions
int SecondChance :: select_victim_frame(){       //process_table[frametable[victim_frame].proccess_id].pte_table[frametable[victim_frame].page_id]

	int selected;
	while(1){

        struct frame *fr = &frame_table[victim_frame];
        struct process *proc = &process_table[fr->proccess_id];
		struct pte *newPTE = &proc->pte_table[fr->page_id];
        
        selected = victim_frame;
        victim_frame = (victim_frame + 1) % num_frames;

		if (!newPTE->referenced)
			break;

	    newPTE->referenced = 0;
 	}

 	return selected;
}

/////////////////////////////////////////

class Clock : public Pager{
	public:
		Clock(){
			victim_frame = 0;
		};

		int victim_frame;
		int select_victim_frame();
};

/////////////////Define Clock functions

int Clock :: select_victim_frame(){

	int selected;
	while(1){

        struct frame *fr = &frame_table[victim_frame];
        struct process *proc = &process_table[fr->proccess_id];
		struct pte *newPTE = &proc->pte_table[fr->page_id];
        
        selected = victim_frame;
        victim_frame = (victim_frame + 1) % num_frames;

		if (!newPTE->referenced)
			break;

	    newPTE->referenced = 0;
 	}

 	return selected;
}

////////////////////////////////////////////

class Random : public Pager{	
	public:
		Random(){
			victim_frame = 0;
		};

		int victim_frame;
		int select_victim_frame();
};

///////////////////////Define Random functions
int Random :: select_victim_frame(){

		victim_frame = myrandom(num_frames);
		return (victim_frame);
}

//////////////////////////////////////////////

class Aging : public Pager{
	public:
			Aging(){
				victim_frame = 0;
			};

		int victim_frame;
		int select_victim_frame();
};

////////////////////////Define Aging functions
int Aging :: select_victim_frame(){

    int min = 0;
    
	for(int i = 0; i < num_frames; i++){

		struct frame * aFrame = &frame_table[i];
		struct process * aProc = &process_table[aFrame->proccess_id];
		struct pte * aPTE = &aProc->pte_table[aFrame->page_id];
		struct frame * minFrame = &frame_table[min];

    	//first shift to the right one bit
    	aFrame->age =  aFrame->age >> 1;
    	
    	//if the reference bit is set in the PTE, mask first bit with OR, (1<<31), ox10000000 
   		if (aPTE->referenced){
			aFrame->age = (aFrame->age)  | (1 << 31);
			//reset reference bit if it is set in the PTE
			aPTE->referenced = 0;
		}

		//set min age
		if ((aFrame->age) < (minFrame->age)){
			min = i;
		}
	}

	//return the "youngest" frame (referenced the longest time ago)
 	return min;
}

//////////////////////////////////////////////

class NRU : public Pager{
	public:
		NRU(){
			victim_frame = 0;
		}
		int victim_frame;
		int select_victim_frame(); 
};

/////////////////////Define NRU functions
int NRU :: select_victim_frame(){

	bool reset_refbit = 0;

	// increase the count each time a frame is processed
	NRU_count += 1;

	//vector of arrays, one for each class, 4 classes total (0-3)
	vector <vector <int>> classes(4);

	//loop through the frame table, looking at the PTE for each frame
	for(int n = 0; n < num_frames; n++){

		struct frame * nFrame = &frame_table[n];
		struct process * nProc = &process_table[nFrame->proccess_id];
		struct pte * nPTE = &nProc->pte_table[nFrame->page_id];

		//check the referenced and modified bits
		if((nPTE->referenced) && (nPTE->modified)){
			(classes[3]).push_back(n); //referenced and modified
		}else if((nPTE->referenced)){  
			(classes[2]).push_back(n);  //referenced but not modified 
		}else if(nPTE->modified){
			(classes[1]).push_back(n); //not referenced but modified
		}else{
			(classes[0]).push_back(n);  //not referenced or modified
		}

		//if counter = 10 reset counter back to 0, reset reference bit
		if(NRU_count == 10){
			NRU_count = 0;
			reset_refbit = 1;
		}

		if(reset_refbit){
				nPTE->referenced = 0;
		}
	}

	//loop through each class
	for(int m = 0; m < 3; m++){

		//non zero size array = target class
		if(classes[m].size()){
			//random index into that class for victim frame
			int frame = myrandom(classes[m].size());
			//break the loop, return the victim frame
			victim_frame = (classes[m][frame]);
			return victim_frame;
		}
	}

	return victim_frame;
}

//////////////////////////////////////////////

// read instruction and process / page
void update_pte(char instruction, int p) {
     // either 'c' 'r' 'w' operation

       	if (instruction == 'c') {
           currentProcess = &process_table[p];
           pageTable = currentProcess->pte_table;
           return;
        }

        // now its R or W
        cost += 1;
        currentProcess->pte_table[p].referenced = 1;
       
        if (instruction == 'w') {
      		if(!(pageTable[p].writeProtected))
      			currentProcess->pte_table[p].modified = 1;
      		
       	}
}

/////main function
int main( int argc, char *argv[] ){
	int option;

	//look for a file and open it
	the_file.open(argv[argc -2]);

	if(!the_file.is_open()){
		printf("Can't open the file!\n");
	}else{

		//look for the random file and open it
		random_file.open(argv[argc -1]);

		if(!random_file.is_open()){
			printf("Can't open the random file!\n");
		}

		///////read the first number to determine the number of random numbers 
		random_file >> num_rand;
		randvals = new unsigned int[num_rand];

		//////////read the random file line by line into a global array
		int i;
		for (i = 0; i< num_rand; i++){
			unsigned int number;
			random_file >> number;
			randvals[i] = number;
		}
	
        //////////////read arguments///////////////////////////
		while((option = getopt (argc, argv, "a:o:f:" )) != -1){
			switch(option){
				//alogrithm options
		    	case 'a':{
		        	switch(*optarg){
						case 'f':{
			        	//FIFO
			        		pager = new FIFO();
			        		break;
			        	}case 's':{
		        		//Second Chance 
		        			pager = new SecondChance();
		        			break;
		        		}case 'c':{
		        		//clock 
		        			pager = new Clock();
		        			break;
		        		}case 'r': {
		        		//Random 
		        			pager = new Random();
		        			break;				  
		        		}case 'n':{
		        		//NRU 
		        			pager = new NRU();
		        			break;

		        		}case 'a':{
		        		//aging 
		        			pager = new Aging();
		        			break;
		        		}default:{
		        		//default
		        			pager = new FIFO();
		        			break;
		        		}
		        	}
		        	break;
				}case 'o':{
		        	for(char * cptr = optarg; *cptr; cptr++){
		        		switch(*cptr){
		        			case'O':{
		        				//output
		        				O_flag = true;
		        				break;
		        			}case'P':{
		        				//process info
		        				P_flag = true;
		        				break;
		        			}case 'F':{
		        				//frame table info
		        				F_flag = true;
		        				break;
		        			}case 'S':{
		        				//summary info
		        				S_flag = true;
		        				break;
		        			}
		        		}
		        	}
					break;
				}case 'f':{
					//number of frames
					num_frames = atoi(optarg);
		      		frame_table.resize(num_frames);
		      		break;
				}case '?':{
					pager = new FIFO();
			        break;
				}
			}
		}
		////////////////////////////////////////////////////////////////
		///////////Parse the file//////////////////

		//store the number of processes
		num_processes = atoi(get_next_line(line));
		
		//Loop through each process
		for(int i = 0; i < num_processes; i++){

			//Create pointer to new process
    		process * currentProcessItr = new process;

			//initialize the pte for that process
            initialize(currentProcessItr);

            //set process id
            currentProcessItr->pid = i;

			//store the total number of VMAS for each process
			currentProcessItr->num_VMAS = atoi(get_next_line(line));
            
			//for each process loop through VMAS
			for(int j = 0; j < currentProcessItr->num_VMAS; j++){

				//Create pointer to new VMA
				VMA * currentVMA = new VMA;
                //set VMA values
				sscanf(get_next_line(line),"%d %d %d %d", &currentVMA->start_vpage, &currentVMA->end_vpage, &currentVMA->write_protected, &currentVMA->file_mapped);
				//push that VMA onto vma vector
				currentProcessItr->vma_list.push_back(*currentVMA);
			}

			//push each process onto the process table
			process_table.push_back(*currentProcessItr);
		}

		//loop through instructions
		while(get_next_instruction(&operation, &vpage)){	

			//set global page table to current process' page table
			if (operation == 'c') {
				currentProcess = &process_table[vpage];
				pageTable = process_table[vpage].pte_table;
				cost += 121;
           		ctx_switches += 1;
				continue;
			}

			// first check whether the vpage is a valid address, i.e. it falls into one  of the VMAs
			int vma_index = page_to_vma();

			//if the vpage is invalid throw a segmentation violation
			if (vma_index == -1){
				if (O_flag){
					printf(" SEGV\n");
				}
				currentProcess->pstats.segv += 1;
			    cost += 241;
				continue;
			}

		    // if the vpage is not present in the current processes' pagetable then get a new frame   
		   	if(!pageTable[vpage].present){  
				
	     		//vpage is not present, so allocate victim frame index
	        	int newFrame = get_frame_num();  
	        	
				//victim frame
				frame* ret_frame = &frame_table[newFrame];
				
				//if the victim frame is mapped by any process
			 	if(ret_frame->proccess_id >= 0) {
			 		//the file is mapped by a process so it needs to be unmapped before it can get mapped 
			 		if (O_flag){
			 			printf( " UNMAP %d:%d\n", ret_frame->proccess_id, ret_frame->page_id); 
				 	}
        			process_table[ret_frame->proccess_id].pte_table[ret_frame->page_id].present = 0;
        			process_table[ret_frame->proccess_id].pstats.unmaps += 1;
        			cost += 400;

        			//if the file was modified:
        			if (process_table[ret_frame->proccess_id].pte_table[ret_frame->page_id].modified) {

		        		if(process_table[ret_frame->proccess_id].pte_table[ret_frame->page_id].fileMapped){
							if (O_flag){
								printf(" FOUT\n");  //..and file mapped, write the file back 
							}
		    				process_table[ret_frame->proccess_id].pstats.fouts +=1;
		    				cost += 2500;
		    			}else{
		    				if (O_flag){
        					printf(" OUT\n");
	        				}
	        				process_table[ret_frame->proccess_id].pte_table[ret_frame->page_id].pagedout = 1;
	        				process_table[ret_frame->proccess_id].pstats.outs += 1;
	        				cost += 3000;
		    			}
		    			process_table[ret_frame->proccess_id].pte_table[ret_frame->page_id].modified = 0;
		    			process_table[ret_frame->proccess_id].pte_table[ret_frame->page_id].referenced = 0;
					}
				}	

		    	if (pageTable[vpage].fileMapped){
		    		if (O_flag){
		    			printf(" FIN\n"); //check to see if frame is paged out AND a memory mapped file
		    		}
		    		currentProcess->pstats.fins += 1;
		    		cost += 2500;
		    	}else if((pageTable[vpage].pagedout)){
		    		if (O_flag){
		    			printf(" IN\n"); //check to see if frame was previously paged out BUT NOT file mapped, if so page must be brought back from the swap space
		    		}
		    		currentProcess->pstats.ins += 1;
		    		cost += 3000;
			    }else{
			    	if (O_flag){
			    		printf(" ZERO\n"); //if vpage was never swapped out and is not file mapped so it still has zero filled content
			    	}
			    	currentProcess->pstats.zeros += 1;
			    	cost += 150;
		    	}

	        	//regardless you need to map the new frame
	        	if (O_flag){
	        		printf(" MAP %d\n", newFrame);
	        	}
	        	currentProcess->pstats.maps += 1; 
	        	cost += 400;
	        	//initialize age of frame to zero every time you map a frame
	        	frame_table[newFrame].age = 0;        
	        	//forward map page table to victim frame 
				pageTable[vpage].frameAddress = newFrame;
				pageTable[vpage].present = 1;
				pageTable[vpage].referenced = 0;
				pageTable[vpage].modified = 0;
				//forward map frame table [physical memory] to current process ID and virtual memory page
				ret_frame->proccess_id = currentProcess->pid;
				ret_frame->page_id = vpage;
			}

			//if write protected and write operation, return write protected error
			if((pageTable[vpage].writeProtected) && (operation == 'w')){   
				//is so, print segmentation fault
				if (O_flag){
					printf(" SEGPROT\n");

				}
				currentProcess->pstats.segprot += 1;
				cost += 300;
			}

			//don't forget to update the page table!
		    update_pte(operation, vpage); 
		}
	}


	for(int l=0; l < num_processes; l++){
		printf("PT[%d]: ", l);
		for(int m=0; m < 64; m++){

			if(process_table[l].pte_table[m].present){
				printf("%d:", m);
				if (process_table[l].pte_table[m].referenced){
					printf("R");
				}else{
					printf("-");
				}
				
				if(process_table[l].pte_table[m].modified){
					printf("M");			
				}else{
					printf("-");
				}

				if(process_table[l].pte_table[m].pagedout){
					printf("S ");
				}else{
					printf("- ");
				}
				
			}else if(! (process_table[l].pte_table[m].pagedout)){
					printf("* ");
			}else{
				printf("# ");
			}
		}
		printf("\n");
	}


	if(F_flag){
		printf("FT: ");
		for(int i=0; i < num_frames; i++){
			if(frame_table[i].proccess_id == -1){
				printf("* ");
			}else{
				printf("%d:",frame_table[i].proccess_id);
				printf("%d ",frame_table[i].page_id);
			}
		}
		printf("\n");
	}

	if (P_flag){
		for(int k=0; k<num_processes; k++){
			stats itr = process_table[k].pstats;
			//print stats for each process
			printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n", k, itr.unmaps, itr.maps, itr.ins, itr.outs, itr.fins, itr.fouts, itr.zeros, itr.segv, itr.segprot);
		}
	} 

	if(S_flag){
		//print summary
		printf("TOTALCOST %lu %lu %llu\n", ctx_switches, (iCount + 1), cost);
	
	}

}



