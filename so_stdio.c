#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "so_stdio.h"

#define BUFFSIZE 4096

struct _so_file {

	int fd;
	int filePos;

	unsigned flags;
	unsigned char hold;

	unsigned char buffer[BUFFSIZE];
	int bufferPos;
	int writePos;
	int writeLen;

	int error;
	int eofPos;

};

SO_FILE *so_fopen(const char *pathname, const char *mode){

	int fd, filePos = 0;

	if (strcmp(mode, "r") == 0) {
		fd = open(pathname, O_RDONLY);
	} else if (strcmp(mode, "r+") == 0) {
		fd = open(pathname, O_RDWR);	
	} else if (strcmp(mode, "w") == 0) {
		fd = open(pathname, O_WRONLY | O_TRUNC | O_CREAT, 0666);
	} else if (strcmp(mode, "w+") == 0) {
		fd = open(pathname, O_RDWR | O_TRUNC | O_CREAT, 0666);
	} else if (strcmp(mode, "a") == 0) {
		fd = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0666);
		filePos = lseek(fd, 0, SEEK_END);
	} else if (strcmp(mode, "a+") == 0) {
		fd = open(pathname, O_RDWR | O_APPEND | O_CREAT, 0666);
		filePos = lseek(fd, 0, SEEK_END);
	} else
		return NULL;

	if (fd == -1) {
		printf("File does not exist\n");
		return NULL;
	}


	SO_FILE *myFile = calloc(1, sizeof(SO_FILE));
	if (myFile == NULL) {
		close(fd);
		return NULL;
	}

	myFile->fd = fd;
	myFile->filePos = filePos;
	memset(myFile->buffer, 0, BUFFSIZE);
	myFile->error = 0;
	myFile->eofPos = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	myFile->bufferPos = 0;
	myFile->writePos = 0;
	myFile->writeLen = 0;
	

	return myFile;
}

int so_fclose(SO_FILE *stream){

	if (stream == NULL)
		return SO_EOF;

	if (stream->fd == -1)
		return SO_EOF;
	
	if (stream->writePos > 0 && so_fflush(stream) < 0) {
		stream->error = 1;
		return SO_EOF;
	}

	int fd = close(stream->fd);
	if (fd == -1)
		return SO_EOF;
	
	free(stream);
	return 0;
}

int so_fileno(SO_FILE *stream){
	if (stream == NULL)
		return SO_EOF;

	return stream->fd;
}

int so_fflush(SO_FILE *stream){
	if (stream == NULL)
		return SO_EOF;

	int rc = write(stream->fd, stream->buffer, stream->writeLen);

	if (rc < 0) {
		stream->error = 1;
		return SO_EOF;
	}

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence){
	stream->error = 1;
	return 0;
}

long so_ftell(SO_FILE *stream){

	stream->error 	= 1;
	return -1;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream){
	int bytesWritten = 0;

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	int bytesToBeRead = nmemb * size;

	for (int i = 0; i < bytesToBeRead; i++) {
		int rc = so_fgetc(stream);
		bytesWritten += rc;
		//printf("- %d: %c\n", i, rc);
		// either error or EOF from fgetc
		if (rc == SO_EOF){
			//printf("here %d\n", i);
			stream->error = 1;
			return 0;
		}
		*((char *)ptr + i) = rc;
		//memcpy(ptr + i, stream->buffer + stream->bufferPos, 1);

	}

	return nmemb / size;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream){
	
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	int bytesToBeWritten = size * nmemb;

	for (int i = 0; i < bytesToBeWritten; i++) {
		char c = *((char *)ptr + i);
		int rc = so_fputc(c, stream);
		if (rc == SO_EOF) {
			stream->error = 1;
			return 0;
		}

	}
	return nmemb;
}

int so_fgetc(SO_FILE *stream){

	unsigned char cChar;
	int bytesRead = 0;

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->fd == -1){
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->bufferPos == 0) {
	 	bytesRead = read(stream->fd, stream->buffer, BUFFSIZE);
	 	printf("rc = %d\n", bytesRead);
			if (bytesRead == -1) {
				//stream->eofPos = 1;
				stream->error = 1;
				return SO_EOF;
			}
			if (bytesRead == 0)
				printf("end of file\n");
	
	}
	stream->filePos++;

	cChar = stream->buffer[stream->bufferPos];
	
	if (cChar == 0) {
		//stream->eofPos = 1;
		return SO_EOF;
	}
	
	//printf(stream->bufferPos)
	stream->bufferPos++;
	if (stream->bufferPos == bytesRead && bytesRead < BUFFSIZE) {
		printf("here\n");
		stream->bufferPos = 0;
	}

	if (stream->bufferPos == BUFFSIZE) {
		printf("here2\n");
		stream->bufferPos = 0;
	}
	
	
	return (int)cChar;
}

int so_fputc(int c, SO_FILE *stream){

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	if (stream->fd == -1){
		stream->error = 1;
		return SO_EOF;
	}


	if (stream->writePos == BUFFSIZE) {
		printf("here\n");
		int rc = write(stream->fd, stream->buffer, stream->writeLen);

		if (rc < 0) {
			stream->error = 1;
			return SO_EOF;
		}
		strcpy(stream->buffer,"");
		stream->writeLen = 0;
		stream->writePos = 0;

	}
	//write(stream->fd, buff, len);
	stream->buffer[stream->writePos] = (char)c;
	//printf("%c ..... ", stream->buffer[stream->writePos]);
	stream->writeLen++;
	stream->writePos++;

	// stream->filePos++;

	return c;
}

int so_feof(SO_FILE *stream){

	if (stream == NULL)
		return SO_EOF;
	
	int posOfFilePointer = ftell(stream);

	if ((stream->eofPos  == posOfFilePointer) && (stream->eofPos != 0)) {
		return 1;
	}

	return 0;
}

int so_ferror(SO_FILE *stream){

	if(stream == NULL)
		return SO_EOF;
	

	return stream->error;
}

SO_FILE *so_popen(const char *command, const char *type){
	return NULL;
}

int so_pclose(SO_FILE *stream){
	return 0;
}
