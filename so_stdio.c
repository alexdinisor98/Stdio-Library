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
#define READ 0
#define WRITE 1

struct _so_file {

	int fd;
	int filePos;

	unsigned char buffer[BUFFSIZE];
	int bufferPos;
	int writePos;
	int writeLen;

	int error;
	int eofPos;
	int feof;

	int bytesRead;
	int lastOp;

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
	myFile->bytesRead = 0;
	myFile->lastOp = -1;

	myFile->feof = -3;
	

	return myFile;
}

int xwrite(int fd, const void *buf, size_t count)
{
    size_t bytes_written = 0;

    /* While write returns less than count bytes check for:
     *  - <=0 => I/O error
     *  - else continue writing until count is reached
     */
    while (bytes_written < count) {
        int bytes_written_now = write(fd, buf + bytes_written,
                                          count - bytes_written);

        if (bytes_written_now <= 0)
            return -1;

        bytes_written += bytes_written_now;
    }

    return bytes_written;
}

ssize_t xread(int fd, void *buf, size_t count)
{
    size_t bytes_read = 0;

    while (bytes_read < count) {
        ssize_t bytes_read_now = read(fd, buf + bytes_read,
                                      count - bytes_read);

        if (bytes_read_now == 0) /* EOF */
            return bytes_read;

        if (bytes_read_now < 0) /* I/O error */
            return -1;

        bytes_read += bytes_read_now;
    }

    return bytes_read;
}

int so_fclose(SO_FILE *stream){

	if (stream == NULL)
		return SO_EOF;

	if (stream->fd == -1) {
		free(stream);
		return SO_EOF;
	}

	int rc = so_fflush(stream);

	if (stream->writePos > 0 && rc < 0) {
		stream->error = 1;
		free(stream);
		return SO_EOF;
	}

	int fd = close(stream->fd);
	if (fd == -1) {
		free(stream);
		return SO_EOF;
	}
	
	free(stream);
	return 0;
}

int so_fileno(SO_FILE *stream){
	if (stream == NULL)
		return SO_EOF;

	return stream->fd;
}

int so_fflush(SO_FILE *stream) {
	if (stream == NULL)
		return SO_EOF;

	if (stream->lastOp == WRITE) {
		int rc = xwrite(stream->fd, stream->buffer, stream->writeLen);
		//printf("flush here \n");
		if (rc < 0) {
			stream->error = 1;
			//free(stream);
			return SO_EOF;
		}
	}

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence){
	if (stream == NULL){
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->fd == -1){
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->lastOp == WRITE)
		so_fflush(stream);

	if (stream->lastOp == READ) {
		stream->bufferPos = 0;
	}


	if ((whence == SEEK_CUR) || (whence == SEEK_SET) || (whence == SEEK_END)) {
		int ls = lseek(stream->fd, offset, whence);
		if (ls < 0) {
			stream->error = 1;
			return SO_EOF;
		}
		stream->filePos += ls;
	}else
		return SO_EOF;

	return 0;
}

long so_ftell(SO_FILE *stream) {
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	
	return stream->filePos;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream) {

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if(stream->lastOp == WRITE){
		so_fflush(stream);
	}
	stream->lastOp = READ;

	int bytesToBeRead = nmemb * size;

	for (int i = 0; i < bytesToBeRead; i++) {
		int rc = so_fgetc(stream);
		//bytesWritten += rc;
		//printf("- %d: %c\n", i, rc);
		// either error or EOF from fgetc
		if (rc == SO_EOF){			
			stream->error = 1;
			return 0;
		}
		if (rc == -2){			
			stream->feof = 0;
			return nmemb / size;
		}
		*((char *)ptr + i) = rc;
		//memcpy(ptr + i, stream->buffer + stream->bufferPos, 1);

	}

	return nmemb / size;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream) {
	
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if(stream->lastOp == READ){
		stream->bufferPos = 0;
		so_fflush(stream);
	}
	stream->lastOp = WRITE;

	int bytesToBeWritten = size * nmemb;
	//printf("1\n");
	for (int i = 0; i < bytesToBeWritten; i++) {

		char c = *((char *)ptr + i);
		int rc = so_fputc(c, stream);
		//printf("2\n");
		// if (rc == SO_EOF) {
		// 	printf("3\n");
		// 	stream->error = 1;
		// 	return 0;
		// }

	}
	return nmemb;
}

int so_fgetc(SO_FILE *stream){

	unsigned char cChar;
	//int bytesRead = 0;

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->fd == -1){
		stream->error = 1;
		return SO_EOF;
	}

	if(stream->lastOp == WRITE){
		so_fflush(stream);
	}

	stream->lastOp = READ;

	if (stream->bufferPos == 0) {
	 	stream->bytesRead = read(stream->fd, stream->buffer, BUFFSIZE);
	 	//printf("rc = %d\n", stream->bytesRead);
			if (stream->bytesRead == -1) {
				//stream->eofPos = 1;
				stream->error = 1;
				return SO_EOF;
			}
			if (stream->bytesRead == 0) {
				//stream->eofPos = 
				printf("end of file\n");
				return -2;
			}
	
	}
	stream->filePos++;

	cChar = stream->buffer[stream->bufferPos];

	stream->bufferPos++;
	if (stream->bufferPos == stream->bytesRead) {
		//printf("here\n");
		stream->bufferPos = 0;
	}
		
	return (int)cChar;
}

int so_fputc(int c, SO_FILE *stream) {

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	if (stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->lastOp == READ)
		stream->writePos = 0;

	stream->lastOp = WRITE;


	if (stream->writePos == BUFFSIZE) {
		int rc = xwrite(stream->fd, stream->buffer, stream->writeLen);

		if (rc < 0) {
			stream->error = 1;
			return SO_EOF;
		}
		strcpy(stream->buffer,"");
		stream->writeLen = 0;
		stream->writePos = 0;
	}

	stream->buffer[stream->writePos] = (char)c;
	stream->writeLen++;
	stream->writePos++;

	stream->filePos++;
	return c;
}

int so_feof(SO_FILE *stream) {

	if (stream == NULL)
		return SO_EOF;

	if (stream->fd == -1) {
		printf("here1\n");
		//free(stream);
		return SO_EOF;
	}
	long posOfFilePointer = so_ftell(stream);
	if (stream->feof == 0) {
		return 1;
	}

	if (stream->eofPos  == posOfFilePointer) {
		printf("%d\n", stream->eofPos);
		printf("here4\n");
		return 1;
	}

	return 0;
}

int so_ferror(SO_FILE *stream){

	if (stream == NULL)
		return SO_EOF;
	

	return stream->error;
}

SO_FILE *so_popen(const char *command, const char *type){
	return NULL;
}

int so_pclose(SO_FILE *stream){
	return 0;
}
