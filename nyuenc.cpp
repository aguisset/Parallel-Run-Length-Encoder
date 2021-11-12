#include<iostream>
#include <queue>

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

queue<string> taskQueue; // will hold file chunk
pthread_mutex_t mutexQueue;
pthread_cond_t condQueue;
int slice_count = 1;
/*
int read(char *buf, int n) {
	// Given a file it reads n character 4 bytes at a time
    int copiedChars = 0, readChars = 4;
    char buf4[FIXED_SIZE_CHUNKS]; // 4KB = 4000B
    
    while (copiedChars < n && readChars == 4) {
        readChars = read4(buf4); // need to change read4
        
        for (int i = 0; i < readChars; ++i) {
            if (copiedChars == n)
                return copiedChars;
            buf[copiedChars] = buf4[i];
            ++copiedChars;    
        }    
    }
    return copiedChars;
}
*/
string compress_string(const char* str, int n){
	/* This function writes to stdout the encoded string and returns it for safe keeping [1]*/

	const char* encoded;
	string res = "";
    for (int i = 0; i < n; i++) {
 		
        // Count occurrences of current character
        unsigned char count = 1;
        while (i < n - 1 && str[i] == str[i + 1]) {
            count++;
            i++;
        }
        //cout << str[i];
        //cout << count;
        //cout << count;
 		res+= str[i];
 		res+= count;
 		
 		//res+=count;
    }
    
    encoded = res.c_str();
    return res; // to convert a string to char*
}


const char* encode_file(char* file_name){
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
	
	
	//printf("Orginal file: "); // for debug
	//print_file(file_in_memory, sb.st_size); // for debug

	// do what you want with file
	//const char* encoded = compress_string(file_in_memory, sb.st_size);

	munmap(&file_in_memory, sb.st_size);
	close(fd);
	return file_in_memory;

}
string concatenate(char** argv, int argc){
	/**
	 * 
	 *  Wrapper function for encode_file
	 * */
	string str = "";
	for(int i = 1; i < argc; i++){
		str += encode_file(argv[i]);
	}

	//const char* str_to_encode = str.c_str();
	//compress_string(str_to_encode, str.length());
	return str;
}

void submitTask(string slice){
	// Submit a new slice of string to the queue
	pthread_mutex_lock(&mutexQueue);// to avoid race conditions
	taskQueue.push(slice);
	pthread_mutex_unlock(&mutexQueue);
	pthread_cond_signal(&condQueue);
}
void* start_thread(void* args){
	// wait and execute task
		char* encoded_slice = (char*) malloc(FIXED_SIZE_CHUNKS*sizeof(char)); // at most
		string slice;
		pthread_mutex_lock(&mutexQueue);

		while(taskQueue.empty()){
			pthread_cond_wait(&condQueue, &mutexQueue);
		}
		
		slice = taskQueue.front();
		taskQueue.pop();
		

		// execute the task
		const char* str_to_encode = slice.c_str();
		strncpy(encoded_slice, compress_string(str_to_encode, slice.length()).c_str(), FIXED_SIZE_CHUNKS*sizeof(char));
		//strncpy(encoded_slice,compress_string(str_to_encode, slice.length()), FIXED_SIZE_CHUNKS*sizeof(char));
		//strncpy(encoded_slice, "abdoul", FIXED_SIZE_CHUNKS*sizeof(char)); // for debug

		pthread_mutex_unlock(&mutexQueue);
		slice_count--;
		
		//if(slice_count == 0) pthread_exit(NULL);
		return (void*)encoded_slice;
	
}
int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "Not enough arguments passed. [Usage]: ./nyuenc file1.text [file2.txt]\n");
		exit(1);
	}

	// init
	pthread_mutex_init(&mutexQueue, NULL);
	pthread_cond_init(&condQueue, NULL);
	// Step 1: Retrieve element from command line and concatenate all files string
	const int THREAD_NUM = 1; // retrieve thread Number from command line
	char* res = NULL;

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

	string string_to_encode(concatenate(argv, argc)); // we can change that steps later on to send each file to a thread
	//cout << "concatenated string is: " << string_to_encode << endl; // for debug (works for string)
	submitTask(string_to_encode); // add the slice to the queue

	// Join threads
	for(int i = 0; i < THREAD_NUM; i++){
		if(pthread_join(th[i], (void**) &res) != 0){
			fprintf(stderr, "Error: Threads could not be joined\n");
			exit(1);
		}
	}

	cout << res << endl;

	pthread_mutex_destroy(&mutexQueue);
	pthread_cond_destroy(&condQueue);

	// free what has been dynamically allocated in the thread
	if(res != NULL)
		free(res);

	return 0;
}

/*
Sources:
[1] Run Length encoding: https://www.geeksforgeeks.org/run-length-encoding/
[2] How to Map files into Memory: https://www.youtube.com/watch?v=m7E9piHcfr4
*/