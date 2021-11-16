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
	
	
	for(MappedFile mappedFile: fileList){
		int substring_count = mappedFile.size / split_length;
		
		for(int i = 0; i < substring_count; i++){
			//slices.push_back(str.substr(i*split_length, split_length));

			Task task = {
				.taskId = timestamp++,
				.startAddr = mappedFile.startAddr+ (i*split_length),
				.length = split_length
				//.slice = str.substr(i*split_length, split_length)
			};
			
			if(isSeq) seqQueue.push(task);
			else submitTask(task); // multi threaded
		}

		// if there are leftover characters, create a shorter item at the end
		if(mappedFile.size % split_length != 0){
			//debugLeft[currenTask.taskId] = currenTask;// for debug
			Task task = {
				.taskId = timestamp++,
				.startAddr = mappedFile.startAddr + (split_length * substring_count),
				.length = static_cast<int>(mappedFile.size % split_length)
				//.slice = str.substr(split_length * substring_count)
			};
			//cout << "split_length is: " << split_length << endl; // for deb
			//cout << "MappedFile.size: " << mappedFile.size << endl; // for deb
			
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
        unsigned char count = 1;
        //int count = 1; // for easier debug
        while (i < n - 1 && str[i] == str[i + 1]) {
            count++;
            i++;
        }
        //cout << str[i];
        //cout << count;
        //cout << count;
 		if(i < n) res+= str[i];
 		res+= count;
 		//res+= to_string(count); // add to string when having int for debug

 		//res+=count;
    }
    
    encoded = res.c_str();

    return res; // to convert a string to char*
}

string compress_string2(const char* str, int start, int n){
	/* This function writes to stdout the encoded string and returns it for safe keeping [1]*/

	const char* encoded;
	string res = "";
    for (int i = start; i < n; i++) {
        // Count occurrences of current character
        unsigned char count = 1;
        //int count = 1; // for easier debug
        while (i < n - 1 && str[i] == str[i + 1]) {
            count++;
            i++;
        }
        //cout << str[i];
        //cout << count;
        //cout << count;
 		if(i < n) res+= str[i];
 		res+= static_cast<unsigned char>(count);
 		//res+= to_string(count); // add to string when having int for debug

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

string stitch(string carry, string str2){
	/*concatenate str1 with str2 in a correct encoded format*/
	string res = "";
	
	if(carry.length() == 0) return str2;

	// find start and end to stitch
	
	// append to the carry the correct count
	if(carry[0] != str2[0]){
		// Not same character between carry and toStich (ex: a2 and b2) => we can safely concatenate both
		res += carry;
		res += str2;
	}
	else{
		// equal
		res += carry[0]; // add first char
		//int count1 = stoi(str1.substr(1, str1.length()));
		//int count2 = stoi(str2.substr(1,end));
		//res += to_string(count1 + count2);
		res += carry[1] + str2[1]; // since it is stored as a 1 byte char
		res += str2.substr(2, str2.length()); // add the rest
	}

	return res;

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

void run_sequentially_old(char** argv, int argc, int THREAD_NUM){
	
	string str = "";
	for(int i = 1; i < argc; i++){
		str += memory_map_helper(argv[i]).startAddr;
	}

	cout << compress_string(str.c_str(), str.length());
	
	return;
}

void run_sequentially(char ** argv, int argc, const int THREAD_NUM){
	vector<MappedFile> fileList = build_file_list(argv, argc, THREAD_NUM);
	slice_by_char(fileList, FIXED_SIZE_CHUNKS, true);
	int j = 1;
	string carry = "";
	string cat = "";
	while(!seqQueue.empty()){
		Task task = seqQueue.front();
		string enc = compress_string(task.startAddr, task.length);
		
		/*
		printf("Sliced %d\n", j);
		for(int i = 0; i < task.length; i++)
			cout << task.startAddr[i];
		*/

		/*
		cout << " with a length " << task.length;
		cout << " Encoded: " << enc;
		cout << endl;
		cout<< endl;
		*/

		/*
		cout << "===== stitching phase======" << endl;
		cout << "carry = " << carry << endl;
		*/
		cat = stitch(carry,enc);
		
		/*
		cout << j << "-th Stitched encoded: " << cat << endl;
		cout << endl;

		cout << "What we display: " << cat.substr(0, cat.length() -2) << endl;
		*/
		cout << cat.substr(0, cat.length() -2);
		cout.flush();
		carry = cat.substr(cat.length() -2);
		j++;
		seqQueue.pop();
	}

	if(carry.length() != 0){
		//cout << "Display remaining carry: " << endl;
		cout << carry;
		cout.flush();
	}
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
	// smaller on 
	file_list = build_file_list(argv, argc, THREAD_NUM);
	slice_by_char(file_list, FIXED_SIZE_CHUNKS, false);

	while(taskCounter < timestamp){
		pthread_mutex_lock(&mutexResMap);
		while(map.find(taskCounter) == map.end()){ // Wait until the element with id equal to taskCounter is added in the map
			pthread_cond_wait(&condResQueue, &mutexResMap);
		}

		ResTask resTask = map[taskCounter];

		// stitch with carry
		string stitched = stitch(carry, resTask.encStr);
		
		// what we display
		cout << stitched.substr(0, stitched.length() - 2);
		cout.flush();

		// Update the carry take everything until last char
		carry = stitched.substr(stitched.length() -2); // add the rest
	
		cout.flush();
		map.erase(taskCounter);
		taskCounter++;
		pthread_mutex_unlock(&mutexResMap);
	}

	
	if(carry.length() != 0){
		// carry is not empty just display it
		cout << carry;
	}
	

	//cout.flush();
	
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