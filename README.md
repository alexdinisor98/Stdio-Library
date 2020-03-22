# Stdio-Library
A minimalist stdio library in C

so_fopen: creating at pathname in the given mode the file to be read/written/
appended and initiating the struct fields.

so_fclose: before closing the stream, it is necessary to fflush (write the data 
left from the buffer if the last operation was WRITE).

so_fflush: write the data left in the buffer, using lastOp field for operation
types.

so_fseek: using the system call 'lseek' to change the location of the read/write 
pointer of the stream file descriptor.

so_ftell: returns the filePosition (increased at reading the file data)

so_fgetc: reads maximum of BUFFSIZE chars and increasing the buffer position
at each call of the function. Buffer position points back to 0 when buffer is
full with the number of chars from the read call.

so_fread: reads nmemb * size chars using function so_fgetc.

so_putc: stream buffer is populated with chars and updating the char length) 
until buffer is full => write in the stream.

so_fwrite: writes in the stream using so_fputc function.

so_feof: compare current position (using ftell system call) of the stream with
the end of the file.

so_ferror: returns 'error' field, updated at each error from every function 
occured.
