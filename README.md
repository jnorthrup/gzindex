# gzindex
Mark Adler's GzIndex Gzip Indexer


This is a thing i found at https://stackoverflow.com/a/54602723

| Function               | Parameters                             | Description                                                                 | Callee(s)                             | Caller(s)              |
|------------------------|----------------------------------------|-----------------------------------------------------------------------------|---------------------------------------|------------------------|
| main                   | int argc, char **argv                 | The main function that processes command-line arguments and executes tasks. | gzindex, gzunindex, gzextract         | -                      |
| gzindex                | char *name, size_t span               | Creates an index for the specified gzip file.                                | index_point                           | main                   |
| gzunindex              | char *name                             | Removes an index from the specified gzip file.                               | -                                     | main                   |
| gzextract              | char *name, off_t start, off_t end     | Extracts data from the specified gzip file using its index.                 | index_open, index_entry, abort        | main                   |
| index_point            | gzFile file, size_t span, off_t bits  | Creates an index entry point for a gzip file.                                | -                                     | gzindex                |
| index_open             | char *name                             | Opens an indexed gzip file for reading.                                      | -                                     | gzextract              |
| index_entry            | gzFile file, off_t *start, off_t *end  | Retrieves the start and end offsets of an indexed gzip file entry.          | -                                     | gzextract              |
| abort                  | char *msg, int ret                     | Outputs an error message and exits the program with the specified status.    | -                                     | gzextract              |
