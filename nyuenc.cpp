#include<iostream>
#include <queue>
#include <unordered_map>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>

#define BUFFER_SIZE 200
#define FILES_MAX_COUNT 100
#define FIXED_SIZE_CHUNKS 4000

using namespace std;

/*Structures definition*/
typedef struct ResTask{
	int taskId;
	string encStr;
}ResTask;

typedef struct Task{
	int taskId;
	const char* startAddr;
	int length;
	//string slice; // START and END of chunk
}Task;

typedef struct MappedFile{
	off_t size;
	const char* startAddr;
}MappedFile;

void slice_by_char(vector<MappedFile> fileList, int split_length, bool isSeq);
string compress_string(const char* str, int n);
MappedFile memory_map_helper(char* file_name);
vector<MappedFile> build_file_list(char** argv, int argc, const int THREAD_NUM);
void submitTask(Task task);
void submitRes(ResTask subRes);
void* start_thread(void* args);
string stitch(string str1, string str2);
void run_sequentially(char** argv, int argc, const int THREAD_NUM);
int get_thread_number_from_cmdl(char** argv, int argc);

/*Global variables*/
// mutexes
pthread_mutex_t mutexQueue;
pthread_mutex_t mutexResMap;
pthread_cond_t condQueue;
pthread_cond_t condResQueue;

queue<Task> seqQueue; // sequential chunks
queue<Task> taskQueue; // will hold file chunk
vector<ResTask> resultVec;
unordered_map<int, ResTask> map; // keep taskId-> ResTask
int taskCounter = 0;
int timestamp = 0;


void slice_by_char(vector<MappedFile> fileList, int split_length, bool isSeq){
	/*Given a list of mapped file object (startAddr and size of each file mmaped) break each files into chunks of split length*/
	// slice by X char the input string [3]
	timestamp = 0;
	for(MappedFile mappedFile: fileList){
		int substring_count = mappedFile.size / split_length;
		
		for(int i = 0; i < substring_count; i++){
			//slices.push_back(str.substr(i*split_length, split_length));

			Task task = {
				.taskId = timestamp++,
				.startAddr = mappedFile.startAddr + i*split_length,
				.length = split_length
				//.slice = str.substr(i*split_length, split_length)
			};
			
			if(isSeq) seqQueue.push(task);
			else submitTask(task); // multi threaded
			
		}

		// if there are leftover characters, create a shorter item at the end
		if(mappedFile.size % split_length != 0){
			Task task = {
				.taskId = timestamp++,
				.startAddr = mappedFile.startAddr + (split_length * substring_count),
				.length = static_cast<int>(mappedFile.size % split_length)
				//.slice = str.substr(split_length * substring_count)
			};
			if(isSeq) seqQueue.push(task);
			else submitTask(task);
			substring_count++;
		}
	}

	return;
}

string compress_string(const char* str, int n){
	/* This function writes to stdout the encoded string and returns it for safe keeping [1]*/

	const char* encoded;
	string res = "";
    for (int i = 0; i < n; i++) {
        // Count occurrences of current character
        //unsigned char count = 1;
        int count = 1; // for easier debug
        while (i < n - 1 && str[i] == str[i + 1]) {
            count++;
            i++;
        }
        //cout << str[i];
        //cout << count;
        //cout << count;
 		res+= str[i];
 		//res+= count;
 		res+= to_string(count); // add to string when having int for debug

 		//res+=count;
    }
    
    encoded = res.c_str();

    return res; // to convert a string to char*
}


MappedFile memory_map_helper(char* file_name){
	/**
	 * Open one file and map it to memory
	 * 
	 */

	off_t file_size = 0;
	int fd;
	struct stat sb;
	char buffer[BUFFER_SIZE];
	memset(buffer, '\0', BUFFER_SIZE);

	// open the file
	if((fd = open(file_name, O_RDWR)) < 0){
		fprintf(stderr, "Error: Can't open the file\n");
		exit(1);
	}

	// we need the size of the file
	if(fstat(fd, &sb) == -1){
		fprintf(stderr, "Error: fstat failed\n");
		exit(1);
	}

	// call mmap with the size of my file
	const char* file_in_memory = static_cast<const char*> (mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

	if(file_in_memory == MAP_FAILED){
		fprintf(stderr, "Error: mmap failed");
		exit(1);
	}
	
	MappedFile mappedFile = {
		.size = sb.st_size,
		.startAddr = file_in_memory
	};

	//munmap(&file_in_memory, sb.st_size);
	close(fd);
	return mappedFile;

}

vector<MappedFile> build_file_list(char** argv, int argc, const int THREAD_NUM){
	/**
	 * 
	 *  Wrapper function that returns mappedFileList
	 * */
	vector<MappedFile> list;
	if(THREAD_NUM == -1){
		for(int i = 1; i < argc; i++){
			list.push_back(memory_map_helper(argv[i]));
		}
	}
	else{
		for(int i = 3; i < argc; i++){
			list.push_back(memory_map_helper(argv[i]));
		}
	}

	return list;
}

void submitTask(Task task){
	// Submit a new slice of string to the queue
	pthread_mutex_lock(&mutexQueue);// to avoid race conditions
	taskQueue.push(task);
	pthread_cond_signal(&condQueue);
	pthread_mutex_unlock(&mutexQueue);
}
void submitRes(ResTask subRes){
	pthread_mutex_lock(&mutexResMap);// to avoid race conditions
	//resultVec.push_back(subRes);
	map[subRes.taskId] = subRes;
	//cout << subRes.taskId << " " << map[subRes.taskId].encStr << endl; // for debug
	pthread_cond_signal(&condResQueue);
	pthread_mutex_unlock(&mutexResMap);
}
void* start_thread(void* args){
	// wait and execute task

	while(1){
		
		Task task;
		ResTask resTask;
		string slice;
		pthread_mutex_lock(&mutexQueue);

		while(taskQueue.size() == 0){
			pthread_cond_wait(&condQueue, &mutexQueue);
		}

		task =taskQueue.front();
		taskQueue.pop();

		// execute the task
		resTask.taskId = task.taskId;
		resTask.encStr = compress_string(task.startAddr, task.length);
		submitRes(resTask);
		pthread_mutex_unlock(&mutexQueue);
	}
}

string stitch(string str1, string str2){
	/*concatenate str1 with str2 in a correct encoded format*/
	string res = "";
	string toStich = "";
	if(str1.length() == 0) return str2;

	// find start and end to stitch
	int end = 0;
	int count = 0;
	for(end = 0; end < str2.length(); end++){
		if(isalpha(str2[end])) count++;
		if(count > 1) break;
	}


	//if(end < str2.length()) toStich = str2.substr(0, end);
	toStich = str2.substr(0, end);

	// append to the carry the correct count
	if((isalpha(str1[0]) && isalpha(toStich[0])) && (str1[0] != toStich[0])){
		// Not same character between carry and toStich (ex: a2 and b2) => we can safely concatenate both
		res += (str1 + str2);
	}
	else if(isalpha(str1[0]) && isalpha(toStich[0])){
		// equal
		res += str1[0]; // add first char
		int count1 = stoi(str1.substr(1, str1.length()));
		int count2 = stoi(str2.substr(1,end));
		res += to_string(count1 + count2);
		//res += str1[1] + str2[1]; // since it is stored as a 1 byte char
		res+= str2.substr(end, str2.length()); // add the rest
	}

	return res;

}

void run_sequentially(char** argv, int argc, const int THREAD_NUM){
	/*Perfom RLE sequentially*/
	vector<MappedFile> file_list;
	file_list = build_file_list(argv, argc, THREAD_NUM);
	slice_by_char(file_list, FIXED_SIZE_CHUNKS, true);
	string res = "";
	string carry = "";

	while(!seqQueue.empty()){
		Task task = seqQueue.front();
		seqQueue.pop();
		string stitched = stitch(carry,compress_string(task.startAddr, task.length));

		// find last char
		int lastChar = stitched.length() - 1;
		while(lastChar >= 0 && !isalpha(stitched[lastChar])) lastChar--; // find last char
		if(lastChar >= 0) carry = stitched.substr(lastChar, stitched.length()); // add the rest

		
		for(int i = 0; i < lastChar; i++){
			cout << stitched[i];
			cout.flush();
		}
		
	}

	if(carry.length() != 0){
			// carry is not empty just display it
			cout << carry;
			cout.flush();
	}

	return;
}

int get_thread_number_from_cmdl(char** argv, int argc){
	/*Retrives command line options [4]*/
    int opt;

    while((opt = getopt(argc, argv, "j:")) != -1){ // as long as there is an option
        switch(opt){
            case 'j':
                return atoi(optarg);
            default:
                if(optopt == 'j')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                break;
        }
    }

    return -1; // no such option
}


int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "Not enough arguments passed. [Usage]: ./nyuenc file1.text [file2.txt]\n");
		exit(1);
	}

	// Step 1: Retrieve element from command line and concatenate all files string
	const int THREAD_NUM = get_thread_number_from_cmdl(argv, argc); // retrieve thread Number from command line

	if(THREAD_NUM == -1){
		run_sequentially(argv, argc, THREAD_NUM);
		return 0;
	}

	// init
	pthread_mutex_init(&mutexQueue, NULL);
	pthread_mutex_init(&mutexResMap, NULL);
	pthread_cond_init(&condQueue, NULL);
	pthread_cond_init(&condResQueue, NULL);

	vector<MappedFile> file_list;
	string carry = "";

	// Step 2: Create threads
	pthread_t th[THREAD_NUM];

	for(int i = 0; i < THREAD_NUM; i++){
		if(pthread_create(th + i, NULL, &start_thread, NULL) != 0){
			fprintf(stderr, "Error: Threads were not created properly\n");
			exit(1);
		}
	}

	file_list = build_file_list(argv, argc, THREAD_NUM);
	slice_by_char(file_list, FIXED_SIZE_CHUNKS, false);

	while(taskCounter != timestamp){
		pthread_mutex_lock(&mutexResMap);
		while(map.find(taskCounter) == map.end()){ // Wait until the element with id equal to taskCounter is added in the map
			pthread_cond_wait(&condResQueue, &mutexResMap);
		}

		ResTask resTask = map[taskCounter];

		// stitch with carry
		string stitched = stitch(carry, resTask.encStr);
		
		// Update the carry take everything until last char
		int lastChar = stitched.length() - 1;
		while(lastChar >= 0 && !isalpha(stitched[lastChar])) lastChar--; // find last char
		if(lastChar >= 0) carry = stitched.substr(lastChar, stitched.length()); // add the rest

		for(int i = 0; i < lastChar; i++){
			cout << stitched[i];
			cout.flush();
		}

		map.erase(taskCounter);
		taskCounter++;
		pthread_mutex_unlock(&mutexResMap);
	}

	if(carry.length() != 0){
		// carry is not empty just display it
		cout << carry;
	}
	cout.flush();
	
	// Join threads (not required)

	// not required to destroy
	//pthread_mutex_destroy(&mutexQueue);
	//pthread_mutex_destroy(&mutexResMap);
	//pthread_cond_destroy(&condQueue);
	//pthread_cond_destroy(&condResQueue);

	// free-ing

	return 0;
}

/*
Sources:
[1] Run Length encoding: https://www.geeksforgeeks.org/run-length-encoding/
[2] How to Map files into Memory: https://www.youtube.com/watch?v=m7E9piHcfr4
[3] Split String every X characters: https://stackoverflow.com/questions/25022880/c-split-string-every-x-characters
[4] getopt: https://stackoverflow.com/questions/4796662/how-to-take-integers-as-command-line-arguments
*/