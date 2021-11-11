#include<iostream>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>


#define BUFFER_SIZE 200

using namespace std;

const char* compress_string(const char* str, int n){
	/* This function writes to stdout the encoded string and returns it for safe keeping */
	
	const char* encoded;
	string res = "";
    for (int i = 0; i < n; i++) {
 		
        // Count occurrences of current character
        unsigned char count = 1;
        while (i < n - 1 && str[i] == str[i + 1]) {
            count++;
            i++;
        }
        cout << str[i];
        cout << count;
        //cout << count;
 		res+= str[i];
 		res+= count;
 		
 		//res+=count;
    }
    
    encoded = res.c_str();
    return encoded; // to convert a string to char*
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
void encode(char** argv, int argc){
	/**
	 * 
	 *  Wrapper function for encode_file
	 * */
	string str = "";
	for(int i = 1; i < argc; i++){
		str += encode_file(argv[i]);
	}

	const char* str_to_encode = str.c_str();
	compress_string(str_to_encode, str.length());
	return;
}

int main(int argc, char** argv){
	if(argc < 2){
		fprintf(stderr, "Not enough arguments passed. [Usage]: ./nyuenc file1.text [file2.txt]\n");
		exit(1);
	}
	//argv++; // we don't care about nyuenc file
	// get parameter from command line: E.g ./nyuenc file.txt file2.txt
	encode(argv, argc);
	// perform the encoding: aaabbb -> a3b3 where 3 is encoded a one byte unsigned integer (unsigned char)

	return 0;
}

/*
Sources:
[1] Run Length encoding: https://www.geeksforgeeks.org/run-length-encoding/
[2] How to Map files into Memory: https://www.youtube.com/watch?v=m7E9piHcfr4
*/