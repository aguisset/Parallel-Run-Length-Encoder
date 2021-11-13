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

void submitTask(Task task);


pthread_mutex_t mutexQueue;
pthread_mutex_t mutexResQ;
pthread_cond_t condQueue;
pthread_cond_t condResQueue;



queue<Task> taskQueue; // will hold file chunk
vector<ResTask> resultVec;
unordered_map<int, ResTask> map; // keep taskId-> ResTask

int taskCounter = 0;
int timestamp = 0;

void slice_by_char(vector<MappedFile> fileList, int split_length){
	/*Given a list of mapped file object (startAddr and size of each file mmaped) break each files into chunks of split length*/
	// slice by X char the input string [3]
	// Executed by main thread
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
			
			submitTask(task);
			
		}

		// if there are leftover characters, create a shorter item at the end
		if(mappedFile.size % split_length != 0){
			Task task = {
				.taskId = timestamp++,
				.startAddr = mappedFile.startAddr + (split_length * substring_count),
				.length = static_cast<int>(mappedFile.size % split_length)
				//.slice = str.substr(split_length * substring_count)
			};
			
			submitTask(task);
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
	//printf("File size is %lld\n", sb.st_size); // for debug
	
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
	
	//printf("Orginal file: "); // for debug
	//print_file(file_in_memory, sb.st_size); // for debug

	// do what you want with file
	//const char* encoded = compress_string(file_in_memory, sb.st_size);

	//munmap(&file_in_memory, sb.st_size);
	close(fd);
	return mappedFile;

}

vector<MappedFile> build_file_list(char** argv, int argc){
	/**
	 * 
	 *  Wrapper function that returns mappedFileList
	 * */
	vector<MappedFile> list;
	for(int i = 1; i < argc; i++){
		list.push_back(memory_map_helper(argv[i]));
	}

	//const char* str_to_encode = str.c_str();
	//compress_string(str_to_encode, str.length());
	return list;
}

/*
string concatenate(char** argv, int argc){
	
	string str = "";
	for(int i = 1; i < argc; i++){
		str += memory_map_helper(argv[i]);
	}

	//const char* str_to_encode = str.c_str();
	//compress_string(str_to_encode, str.length());
	return str;
}
*/
void submitTask(Task task){
	// Submit a new slice of string to the queue
	pthread_mutex_lock(&mutexQueue);// to avoid race conditions
	taskQueue.push(task);
	pthread_mutex_unlock(&mutexQueue);
	pthread_cond_signal(&condQueue);
}
void submitRes(ResTask subRes){
	pthread_mutex_lock(&mutexResQ);// to avoid race conditions
	//resultVec.push_back(subRes);
	map[subRes.taskId] = subRes;
	//cout << subRes.taskId << " " << map[subRes.taskId].encStr << endl; // for debug
	pthread_mutex_unlock(&mutexResQ);
	pthread_cond_signal(&condResQueue);
}
void* start_thread(void* args){
	// wait and execute task

	while(1){
		char* encoded_slice = (char*) malloc(FIXED_SIZE_CHUNKS*sizeof(char)); // at most
		Task task;
		ResTask resTask;
		string slice;
		pthread_mutex_lock(&mutexQueue);

		while(taskQueue.size() == 0){
			pthread_cond_wait(&condQueue, &mutexQueue);
		}
		
		task =taskQueue.front();
		//slice = taskQueue.front();
		taskQueue.pop();
		

		// execute the task
		//const char* str_to_encode = slice.c_str();
		resTask.taskId = task.taskId;
		resTask.encStr = compress_string(task.startAddr, task.length);
		//strncpy(encoded_slice, compress_string(str_to_encode, slice.length()), FIXED_SIZE_CHUNKS*sizeof(char));
		//strncpy(encoded_slice,compress_string(str_to_encode, slice.length()), FIXED_SIZE_CHUNKS*sizeof(char));
		//strncpy(encoded_slice, "abdoul", FIXED_SIZE_CHUNKS*sizeof(char)); // for debug
		//cout << "encoded slice: " << resTask.encStr << endl;// for debug
		//cout.flush(); // for debug
		//submitRes(string(encoded_slice));
		submitRes(resTask);
		//cout << encoded_slice << endl;
		pthread_mutex_unlock(&mutexQueue);
	}
	
	
	//if(slice_count == 0) pthread_exit(NULL);
	//return (void*)encoded_slice;
}

int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "Not enough arguments passed. [Usage]: ./nyuenc file1.text [file2.txt]\n");
		exit(1);
	}

	// init
	pthread_mutex_init(&mutexQueue, NULL);
	pthread_mutex_init(&mutexResQ, NULL);
	pthread_cond_init(&condQueue, NULL);
	pthread_cond_init(&condResQueue, NULL);
	
	// Step 1: Retrieve element from command line and concatenate all files string
	const int THREAD_NUM = 2; // retrieve thread Number from command line
	//char** res = NULL;
	vector<MappedFile> file_list;
	char* res = NULL;
	string finalRes = "";
	string carry = "";

	// Step 2: Create threads
	pthread_t th[THREAD_NUM];

	for(int i = 0; i < THREAD_NUM; i++){
		if(pthread_create(th + i, NULL, &start_thread, NULL) != 0){
			fprintf(stderr, "Error: Threads were not created properly\n");
			exit(1);
		}
	}
	// get parameter from command line: E.g ./nyuenc file.txt file2.txt
	//encode(argv, argc);
	// perform the encoding: aaabbb -> a3b3 where 3 is encoded a one byte unsigned integer (unsigned char)

	//string string_to_encode(concatenate(argv, argc)); // we can change that steps later on to send each file to a thread
	file_list = build_file_list(argv, argc);
	//cout << "concatenated string is: " << string_to_encode << endl; // for debug (works for string)
	//submitTask(string_to_encode); // add the slice to the queue
	slice_by_char(file_list, 2);
	//cout << "slice count: " << count << endl; // for debug
	//char** res = (char**) malloc(count*sizeof(char*));

	/* 
	//Checking Task Queue Status (for debug)
	cout << "TaskQueue: " << endl;
	while(!taskQueue.empty()){
		cout << taskQueue.front() << " ";
		taskQueue.pop();
	}
	cout << endl;

	*/
	// return all res
	
	while(taskCounter != timestamp){
		pthread_mutex_lock(&mutexResQ);
		while(map.find(taskCounter) == map.end()){ // Wait until the element with id equal to taskCounter is added in the map
			pthread_cond_wait(&condResQueue, &mutexResQ);
		}
		
		ResTask resTask = map[taskCounter];
		
		// take everything until last char
		int lastChar = resTask.encStr.length() - 1;
		cout << resTask.encStr << " ";
		while(lastChar >= 0 && !isalpha(resTask.encStr[lastChar])) lastChar--; // find last char

		//finalRes+= carry + resultQueue.front().slice.substr(0, lastChar); // excludes the last char (at start carry is empty)
		for(int i = 0; i < lastChar; i++){
			cout << carry << resTask.encStr[i];
		}
		cout << endl;
		//cout.flush();
		carry = resTask.encStr.substr(lastChar, resTask.encStr.length()); // add the rest
		map.erase(taskCounter);
		taskCounter++;
		pthread_mutex_unlock(&mutexResQ);

	}

	// Join threads
	for(int i = 0; i < THREAD_NUM; i++){
		if(pthread_join(th[i], NULL) != 0){
			fprintf(stderr, "Error: Threads could not be joined\n");
			exit(1);
		}
	}

	

	pthread_mutex_destroy(&mutexQueue);
	pthread_mutex_destroy(&mutexResQ);
	pthread_cond_destroy(&condQueue);
	pthread_cond_destroy(&condResQueue);
	// free what has been dynamically allocated in the thread
	if(res != NULL)
		free(res);

	return 0;
}

/*
Sources:
[1] Run Length encoding: https://www.geeksforgeeks.org/run-length-encoding/
[2] How to Map files into Memory: https://www.youtube.com/watch?v=m7E9piHcfr4
[3] Split String every X characters: https://stackoverflow.com/questions/25022880/c-split-string-every-x-characters
*/