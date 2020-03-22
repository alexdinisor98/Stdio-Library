// SPDX-License-Identifier: GPL-2.0
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
#define READ_OP 0
#define WRITE_OP 1
#define FSEEK_OP 2

struct _so_file {

	int fd;
	long filePos;

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

SO_FILE *so_fopen(const char *pathname, const char *mode)
{

	int fd, filePos = 0;
	/* open file depending on the mode */
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

	/* initialize file fields */
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

	myFile->feof = 0;

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

int so_fclose(SO_FILE *stream)
{

	if (stream == NULL)
		return SO_EOF;

	if (stream->fd == -1) {
		stream->error = 1;
		free(stream);
		return SO_EOF;
	}

	/* write buffer data before closing file */
	int rc = so_fflush(stream);

	if (stream->writePos > 0 && rc < 0) {
		stream->error = 1;
		free(stream);
		return SO_EOF;
	}

	/* close the stream*/
	int fd = close(stream->fd);

	if (fd == -1) {
		stream->error = 1;
		free(stream);
		return SO_EOF;
	}

	free(stream);
	return 0;
}

int so_fileno(SO_FILE *stream)
{
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	return stream->fd;
}

int so_fflush(SO_FILE *stream)
{
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	/* write data from buffer */
	if (stream->lastOp == WRITE_OP) {
		int rc = xwrite(stream->fd, stream->buffer, stream->writeLen);

		if (rc < 0) {
			stream->error = 1;
			return SO_EOF;
		}
	}

	if (stream->lastOp == READ_OP)
		stream->bufferPos = 0;


	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}

	/* flush if there are bytes to be written */
	if (stream->lastOp == WRITE_OP) {
		int ff = so_fflush(stream);

		if (ff == SO_EOF) {
			stream->error = 1;
			return SO_EOF;
		}
	}

	if (stream->lastOp == READ_OP)
		stream->bufferPos = 0;

	stream->lastOp = FSEEK_OP;

	/* lseek at whence */
	if ((whence == SEEK_CUR) || (whence == SEEK_SET)
		|| (whence == SEEK_END)) {
		int ls = lseek(stream->fd, offset, whence);

		if (ls < 0) {
			stream->error = 1;
			return SO_EOF;
		}
		/* increase the file pointer with ls */
		stream->filePos += ls;
	} else {
		stream->error = 1;
		return SO_EOF;
	}

	return 0;
}

long so_ftell(SO_FILE *stream)
{
	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	/* position in file */
	return stream->filePos;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	/* flush before reading new data */
	if (stream->lastOp == WRITE_OP) {
		int ff = so_fflush(stream);

		if (ff == SO_EOF) {
			stream->error = 1;
			return 0;
		}
	}

	stream->lastOp = READ_OP;

	size_t bytesToBeRead = nmemb * size;

	/* read all chars: nmemb * size */
	for (int i = 0; i < bytesToBeRead; i++) {
		int rc = so_fgetc(stream);
		/* either error or EOF from fgetc */
		if (rc == SO_EOF) {
			stream->error = 1;
			return 0;
		}
		if (rc == -2) {
			stream->feof = 1;
			return 0;
		}
		*((char *)ptr + i) = rc;

	}

	return nmemb / size;
}

size_t so_fwrite(const void *ptr, size_t size,
	size_t nmemb, SO_FILE *stream)
{

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	/* invalid buffer after reading */
	if (stream->lastOp == READ_OP) {
		stream->bufferPos = 0;
		int ff = so_fflush(stream);

		if (ff == SO_EOF) {
			stream->error = 1;
			return SO_EOF;
		}
	}
	stream->lastOp = WRITE_OP;

	int bytesToBeWritten = size * nmemb;

	/* write in stream (nmemb * size) chars */
	for (int i = 0; i < bytesToBeWritten; i++) {
		char c = *((char *)ptr + i);

		so_fputc(c, stream);
	}

	return nmemb;
}

int so_fgetc(SO_FILE *stream)
{

	unsigned char cChar;

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}

	/* write buffer data using fflush */
	if (stream->lastOp == WRITE_OP) {
		int ff = so_fflush(stream);

		if (ff == SO_EOF) {
			stream->error = 1;
			return SO_EOF;
		}
	}

	stream->lastOp = READ_OP;

	if (stream->bufferPos == 0) {
		/* read the returned number of bytes */
		stream->bytesRead = read(stream->fd, stream->buffer, BUFFSIZE);

			if (stream->bytesRead == -1) {
				stream->error = 1;
				return SO_EOF;
			}
			if (stream->bytesRead == 0) {
				printf("end of file\n");
				return -2;
			}

	}
	/* increase the file pointer */
	stream->filePos++;

	cChar = stream->buffer[stream->bufferPos];

	stream->bufferPos++;
	/* pointer at START when buffer is full */
	if (stream->bufferPos == stream->bytesRead)
		stream->bufferPos = 0;

	return (int)cChar;
}

int so_fputc(int c, SO_FILE *stream)
{

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}
	if (stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->lastOp == READ_OP)
		stream->writePos = 0;

	stream->lastOp = WRITE_OP;


	if (stream->writePos == BUFFSIZE) {
		int rc = xwrite(stream->fd, stream->buffer, stream->writeLen);

		if (rc < 0) {
			stream->error = 1;
			return SO_EOF;
		}
		stream->writeLen = 0;
		stream->writePos = 0;
	}

	/* fill the buffer */
	stream->buffer[stream->writePos] = (char)c;
	/* increase the number of bytes to be written */
	stream->writeLen++;
	stream->writePos++;

	stream->filePos++;
	return c;
}

int so_feof(SO_FILE *stream)
{

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	if (stream->fd == -1) {
		stream->error = 1;
		return SO_EOF;
	}

	/* get current position in file */
	long posOfFilePointer = so_ftell(stream);

	if (stream->feof == 1)
		return 1;

	/* if pointer at end of file */
	if (stream->eofPos  == posOfFilePointer)
		return 1;

	return 0;
}

int so_ferror(SO_FILE *stream)
{

	if (stream == NULL) {
		stream->error = 1;
		return SO_EOF;
	}

	return stream->error;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	return NULL;
}

int so_pclose(SO_FILE *stream)
{
	return 0;
}
