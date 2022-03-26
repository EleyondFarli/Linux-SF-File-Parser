#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VARIANT 85506
#define BUFSIZE 512

#define MAGIC_SIZE 2
#define HEADER_SIZE_SIZE 2
#define SECT_SIZE_SIZE 4
#define SECT_OFFSET_SIZE 4
#define SECT_TYPE_SIZE 1
#define SECT_NAME_SIZE 12
#define NO_OF_SECTIONS_SIZE 1
#define VERSION_SIZE 4

#define INVALID -1
#define MAGIC_ERROR 1
#define VERSION_ERROR 2
#define SECT_NR_ERROR 3
#define SECT_TYPES_ERROR 4

#define FILE_ERROR 1
#define SECTION_ERROR 2
#define LINE_ERROR 3

#define DONT_SEARCH_FOR_SF 0
#define SEARCH_FOR_SF 1

#define NR_OF_VALID_TYPES 7
int VALID_TYPES[7] = {86, 60, 36, 22, 94, 90, 95};

typedef struct __SECTION {
    char* sect_name;
    int sect_type;
    int sect_offset;
    int sect_size;
}SECTION;

typedef struct __HEADER {
    char magic[3];
    int headerSize;
    int noOfSections;
    int version;
    SECTION* sect; 
}HEADER;

int isValidType(int type)
{
    int isValidType = 0;
    for(int j = 0; j < NR_OF_VALID_TYPES; ++j) {
        if (VALID_TYPES[j] == type) {
            isValidType = 1;
        }
    }
    return isValidType;
}

int interpretHeader(char* filePath, HEADER **fileHeader)
{
    int fd = open(filePath, O_RDONLY);
    if (fd < 0) {
        return INVALID;
    }

    //Parse the magic
    //let some magic guide your parsing
    lseek(fd, -2, SEEK_END);
    char magic[3];
    if (read(fd, magic, MAGIC_SIZE) < 0) {
        return INVALID;
    }
    magic[2] = '\0';

    if (strcmp(magic, "Vu") != 0) {
        return MAGIC_ERROR;
    }
    
    //Parse the header size
    lseek(fd, -4, SEEK_END);
    short* headerSize = malloc(sizeof(short));
    if (read(fd, headerSize, HEADER_SIZE_SIZE) < 0) {
        free(headerSize);
        return INVALID;
    }

    //Move to beginning of header
    lseek(fd, -(*headerSize), SEEK_END);
    free(headerSize);

    //Read the version number
    int* version = malloc(sizeof(int));
    if (read(fd, version, VERSION_SIZE) < 0) {
        free(version);
        return INVALID;
    }

    //store version and free buffer
    int versionNr = *version;
    free(version);

    if (versionNr < 54 || versionNr > 126) {
        return VERSION_ERROR;
    }

    //Read the number of sections
    char* nrSections = malloc(sizeof(char));
    if (read(fd, nrSections, NO_OF_SECTIONS_SIZE) < 0) {
        free(nrSections);
        return INVALID;
    }

    //store number of sections and free buffer
    int noOfSections = (int)*nrSections;
    free(nrSections);

    if (noOfSections < 2 || noOfSections > 12) {
        return SECT_NR_ERROR;
    }

    //check section types
    char sectNames[noOfSections][SECT_NAME_SIZE + 1];
    int sectTypes[noOfSections];
    int sectOffsets[noOfSections];
    int sectSizes[noOfSections];
    for (int i = 0; i < noOfSections; i++) {

        //read sect_name
        char name[SECT_NAME_SIZE + 1];
        if (read(fd, name, SECT_NAME_SIZE) < 0) {
            return INVALID;
        }
        name[SECT_NAME_SIZE] = '\0';

        //read the type
        char* typeChar = malloc(sizeof(char));
        if (read(fd, typeChar, SECT_TYPE_SIZE) < 0) {
            free(typeChar);
            return INVALID;
        }

        //store type
        int typeNr = (int)*typeChar;
        free(typeChar);
        
        //check if type is correct
        if (!isValidType(typeNr)) {
            return SECT_TYPES_ERROR;
        }

        //read offset
        int* offset = malloc(sizeof(int));
        if (read(fd, offset, SECT_OFFSET_SIZE) < 0) {
            free(offset);
            return INVALID;
        }
        //store offset and free buffer
        int offsetNr = *offset;
        free(offset);

        //read size
        int* sectSize = malloc(sizeof(int));
        if (read(fd, sectSize, SECT_SIZE_SIZE) < 0) {
            free(sectSize);
            return INVALID;
        }
        //store size and free buffer
        int sizeNr = *sectSize;
        free(sectSize);

        strcpy(sectNames[i], name);
        sectTypes[i] = typeNr;
        sectOffsets[i] = offsetNr;
        sectSizes[i] = sizeNr;
    }   

    //Save info in fileHeader only after all checks pass
    strcpy((*fileHeader)->magic, magic);
    (*fileHeader)->headerSize = *headerSize;
    (*fileHeader)->version = versionNr;
    (*fileHeader)->noOfSections = noOfSections;

    (*fileHeader)->sect = malloc(sizeof(SECTION) * noOfSections);
    for (int i = 0; i < noOfSections; ++i) {
        (*fileHeader)->sect[i].sect_name = malloc(SECT_NAME_SIZE + 1);
        strcpy((*fileHeader)->sect[i].sect_name, sectNames[i]);
        (*fileHeader)->sect[i].sect_type = sectTypes[i];
        (*fileHeader)->sect[i].sect_offset = sectOffsets[i];
        (*fileHeader)->sect[i].sect_size = sectSizes[i];
    }
    close(fd);
    return 0;
}

int parseFile(char filePath[BUFSIZE], HEADER *fileHeader)
{
    if (filePath == NULL) {
        return INVALID;
    }

    // get info 
    struct stat metadata;
	if (stat(filePath, &metadata) < 0) {  
		return INVALID;
	}
	
    // Check if it is a directory
	if (S_ISREG(metadata.st_mode)) { 
		return interpretHeader(filePath, &fileHeader);
	} else {
		return INVALID;
	}
}

int listDir(char* dirName, int hasPermWrite, char* nameStartString, int searchForSFFiles)
{
	DIR* dir;
	struct dirent *dirEntry;
    struct stat inode;
	char name[BUFSIZE];

	dir = opendir(dirName);
	if (dir == 0) {
		return -1;
	}

	// iterate the directory contents
	while ((dirEntry=readdir(dir)) != 0) {
		// build the complete path to the element in the directory
        if (strcmp(dirEntry->d_name, ".") != 0 && strcmp(dirEntry->d_name, "..") != 0) {
            snprintf(name, BUFSIZE, "%s/%s", dirName, dirEntry->d_name);
            lstat (name, &inode);
            
            if (searchForSFFiles == SEARCH_FOR_SF) {
                HEADER *fileHeader = malloc(sizeof(HEADER));
                int verdict = parseFile(name, fileHeader);
                if (verdict == 0) {
                    int canBeShown = 1;
                    for (int i = 0; i < fileHeader->noOfSections; ++i) {
                        if (fileHeader->sect[i].sect_size > 1036) {
                            canBeShown = 0;
                        }
                    }

                    if (canBeShown) {
                        printf("%s\n", &name[2]);
                    }

                    //free fileHeader
                    for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
                        free((*fileHeader).sect[i].sect_name);
                    }
                    free((*fileHeader).sect);
                }
                free(fileHeader);

            } else if (!hasPermWrite || inode.st_mode & S_IWUSR) {
                if (strlen(nameStartString)) {
                    if (strncmp(dirEntry->d_name, nameStartString, strlen(nameStartString)) == 0) {
                        printf("%s\n", &name[2]);
                    }
                } else {
                    printf("%s\n", &name[2]);
                }
            }
        }
	}
	closedir(dir);
    return 0;
}

int listTree(char* dirName, int hasPermWrite, char* nameStartString, int searchForSFFiles)
{
    DIR* dir;
	struct dirent *dirEntry;
	struct stat inode;
	char name[BUFSIZE];

    //list current directory
    if (listDir(dirName, hasPermWrite, nameStartString, searchForSFFiles) < 0) {
        return INVALID;
    }

    dir = opendir(dirName);
	if (dir == 0) {
		return INVALID;
	}

	// search for other directories in the current directory
	while ((dirEntry = readdir(dir)) != 0) {		
        if (strcmp(dirEntry->d_name, ".") != 0 && strcmp(dirEntry->d_name, "..") != 0) {
		    // get info about the directory's element
            snprintf(name, BUFSIZE, "%s/%s", dirName, dirEntry->d_name);
            lstat (name, &inode);
            // check if it's a directory
            if (S_ISDIR(inode.st_mode)) {
                //list it's contents
                if (listTree(name, hasPermWrite, nameStartString, searchForSFFiles) < 0) {
                    return -1;
                }
            }
        }
	}
    return 0;
	closedir(dir);
}

int listFiles(int argc, char* argv[])
{
    int isRecursive = 0;
    int hasPermWrite = 0;
    char dirPath[BUFSIZE];
    char nameStartString[BUFSIZE] = "";

    //Parse command
    char strToCheck[BUFSIZE];
    for (int i = 1; i < argc; ++i)
    {
        strcpy(strToCheck, "recursive");
        if (strcmp(argv[i], strToCheck) == 0) {
            isRecursive = 1;
        }

        strcpy(strToCheck, "path=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(dirPath, &(argv[i][strlen(strToCheck)]));
        }

        strcpy(strToCheck, "has_perm_write");
        if (strcmp(argv[i], strToCheck) == 0) {
            hasPermWrite = 1;
        }

        strcpy(strToCheck, "name_starts_with=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(nameStartString, &(argv[i][strlen(strToCheck)]));
        }
    }

    if (dirPath == NULL) {
        return -1;
    }

    char concatString[BUFSIZE] = "./";
    strcat(concatString, dirPath);
    strcpy(dirPath, concatString);

    // get info 
    struct stat metadata;
	if (stat(dirPath, &metadata) < 0) {  
		return -1;
	}
	
    // Check if it is a directory
	if (S_ISDIR(metadata.st_mode)) { 
		// list directory's contents
		printf("SUCCESS\n");
        if (isRecursive) {
            return listTree(dirPath, hasPermWrite, nameStartString, DONT_SEARCH_FOR_SF);
        } else {
            return listDir(dirPath, hasPermWrite, nameStartString, DONT_SEARCH_FOR_SF);
        }
	} else {
		return -1;
	}
    
    return 0;    
}

void runParseCommand(int argc, char* argv[])
{
    HEADER *fileHeader = malloc(sizeof(HEADER));

    char filePath[BUFSIZE];
    char strToCheck[BUFSIZE];
    for (int i = 1; i < argc; ++i)
    {
        strcpy(strToCheck, "path=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(filePath, &(argv[i][strlen(strToCheck)]));
        }
    }

    char concatString[BUFSIZE] = "./";
    strcat(concatString, filePath);
    strcpy(filePath, concatString);
    int verdict = parseFile(filePath, fileHeader);
    switch (verdict) {
        case INVALID:
            printf("ERROR\nwrong path\n");
            break;
        case MAGIC_ERROR:
            printf("ERROR\nwrong magic\n");
            break;
        case VERSION_ERROR:
            printf("ERROR\nwrong version\n");
            break;
        case SECT_NR_ERROR:
            printf("ERROR\nwrong sect_nr\n");
            break;
        case SECT_TYPES_ERROR:
            printf("ERROR\nwrong sect_types\n");
            break;
        default:
            printf("SUCCESS\n");
            printf("version=%d\n", (*fileHeader).version);
            printf("nr_sections=%d\n", (*fileHeader).noOfSections);

            for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
                printf("section%d: %s %d %d\n", i + 1, (*fileHeader).sect[i].sect_name, (*fileHeader).sect[i].sect_type, (*fileHeader).sect[i].sect_size);
            }

            //free fileHeader sections
            for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
                free((*fileHeader).sect[i].sect_name);
            }
            free((*fileHeader).sect);
            break;
    }
    free(fileHeader);
}

void printStringReversed(char* str, int size)
{
    for (int i = size - 1; i >= 0; --i) {
        printf("%c", str[i]);
    }
}

int printLine(char filePath[BUFSIZE], int sectionNr, int lineNr)
{
    HEADER *fileHeader = malloc(sizeof(HEADER));
    int verdict = parseFile(filePath, fileHeader);

    //count from 0
    --sectionNr;

    if (verdict != 0) {
        free(fileHeader);
        return FILE_ERROR;
    }
    
    int fd = open(filePath, O_RDONLY);
    if (fd < 0) {
        //free fileHeader
        for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
            free((*fileHeader).sect[i].sect_name);
        }
        free((*fileHeader).sect);
        free(fileHeader);

        return FILE_ERROR;
    }

    if (sectionNr > fileHeader->noOfSections) {
        //free fileHeader
        for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
            free((*fileHeader).sect[i].sect_name);
        }
        free((*fileHeader).sect);
        free(fileHeader);

        return SECTION_ERROR;
    }

    int offset = fileHeader->sect[sectionNr].sect_offset;
    lseek(fd, offset, SEEK_SET);

    char buffer[fileHeader->sect[sectionNr].sect_size + 1];
    if (read(fd, buffer, fileHeader->sect[sectionNr].sect_size) < 0) {
        //free fileHeader
        for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
            free((*fileHeader).sect[i].sect_name);
        }
        free((*fileHeader).sect);
        free(fileHeader);

        return FILE_ERROR;
    }
    buffer[fileHeader->sect[sectionNr].sect_size] = '\0';
    
    int currentLine = 1;
    char* line;

    char lineDelimiter = (char) 0x0A;
    char lineDelimiterString[2];
    sprintf(lineDelimiterString, "%c", lineDelimiter);
    lineDelimiterString[1] = '\0';

    line = strtok(buffer, lineDelimiterString);
    if (line == NULL) {
        //free fileHeader
        for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
            free((*fileHeader).sect[i].sect_name);
        }
        free((*fileHeader).sect);
        free(fileHeader);

        return LINE_ERROR;
    }

    //Find the correct line
    while (currentLine != lineNr && line != NULL) {
        line = strtok(NULL, lineDelimiterString);
        ++currentLine;
    }

    if (currentLine != lineNr) {
        //free fileHeader
        for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
            free((*fileHeader).sect[i].sect_name);
        }
        free((*fileHeader).sect);
        free(fileHeader);

        return LINE_ERROR;
    }
    
    //Print resulting line
    printf("SUCCESS\n");
    for (int i = strlen(line) - 1; i >= 0; --i) {
        printf("%c", line[i]);
    }
    printf("\n");

    //free fileHeader
    for (int i = 0; i < (*fileHeader).noOfSections; ++i) {
        free((*fileHeader).sect[i].sect_name);
    }
    free((*fileHeader).sect);
    free(fileHeader);

    return 0;
}

void runExtractCommand(int argc, char* argv[])
{
    //Parse command
    char filePath[BUFSIZE];
    char sectionString[BUFSIZE];
    char lineString[BUFSIZE];

    int sectionNr = 0;
    int lineNr = 0;

    char strToCheck[BUFSIZE];
    for (int i = 1; i < argc; ++i)
    {
        strcpy(strToCheck, "path=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(filePath, &(argv[i][strlen(strToCheck)]));
        }

        strcpy(strToCheck, "section=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(sectionString, &(argv[i][strlen(strToCheck)]));
        }
        sectionNr = atoi(sectionString);

        strcpy(strToCheck, "line=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(lineString, &(argv[i][strlen(strToCheck)]));
        }
        lineNr = atoi(lineString);
    }

    int verdict = printLine(filePath, sectionNr, lineNr);
    switch (verdict) {
        case INVALID:
            printf("ERROR\n");
            break;
        case FILE_ERROR:
            printf("ERROR\ninvalid file\n");
            break;
        case SECTION_ERROR:
            printf("ERROR\ninvalid section\n");
            break;
        case LINE_ERROR:
            printf("ERROR\ninvalid line\n");
            break;
        default:
            break;
    }
}

int listSFFiles(int argc, char* argv[])
{
    char dirPath[BUFSIZE];

    //Parse command
    char strToCheck[BUFSIZE];
    for (int i = 1; i < argc; ++i)
    {
        strcpy(strToCheck, "path=");
        if (strncmp(argv[i], strToCheck, strlen(strToCheck)) == 0) {
            strcpy(dirPath, &(argv[i][strlen(strToCheck)]));
        }
    }

    if (dirPath == NULL) {
        return INVALID;
    }

    char concatString[BUFSIZE] = "./";
    strcat(concatString, dirPath);
    strcpy(dirPath, concatString);

    // get info 
    struct stat metadata;
	if (stat(dirPath, &metadata) < 0) {  
		return INVALID;
	}
	
    // Check if it is a directory
	if (S_ISDIR(metadata.st_mode)) { 
		// list directory's contents
		printf("SUCCESS\n");
        return listTree(dirPath, 0, NULL, SEARCH_FOR_SF);
	} else {
		return INVALID;
	}
    
    return 0;    
}

void interpretArgvStrings(int argc, char* argv[])
{
    if (strcmp(argv[1], "variant") == 0) {
        printf("%d\n", VARIANT);
        return;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "list") == 0) {
            if (listFiles(argc, argv) < 0) {
                printf("ERROR\ninvalid directory path\n");
            }
            return;
        }
        
        if (strcmp(argv[i], "parse") == 0) {
            runParseCommand(argc, argv);
            return;
        }

        if (strcmp(argv[i], "extract") == 0) {
            runExtractCommand(argc, argv);
            return;
        }

        if (strcmp(argv[i], "findall") == 0) {
            if (listSFFiles(argc, argv) < 0) {
                printf("ERROR\ninvalid directory path\n");
            }
            return;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc >= 2) {
        interpretArgvStrings(argc, argv);
        return 0;
    }

    printf("Incorrect usage of %s\n", argv[0]);
    return INVALID;
}