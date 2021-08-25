// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
//MP4 modified
#define NumDirEntries 		64
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG(dbgFile, "Initializing the file system.");
    if (format) {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);	    
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!

		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
		ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
		mapHdr->WriteBack(FreeMapSector);    
		dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
     
		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
        }
        delete freeMap; 
		delete directory; 
		delete mapHdr; 
		delete dirHdr;
    } else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
    //MP4 modified
    currentOpenedFile = NULL;
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool
FileSystem::Create(char *name, int initialSize)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;

    DEBUG(dbgFile, "Creating file " << name << " size " << initialSize);

    DEBUG(dbgFile, "Creating file's absolute path is "<<name);

    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);

    //MP4 modified
    OpenFile *tmpFile = directoryFile; //directoryFile => root directory
    char *token = strtok(name, "/");
    char *prev_token = token;
    bool firstTmp = TRUE;

    while(token != NULL){
        DEBUG(dbgFile, "token not null so in while");
        sector = directory -> Find(token);
        if(sector == -1)break;
        DEBUG(dbgFile, "current directory is " << token);
        if(firstTmp)firstTmp = FALSE;
        else delete tmpFile;
        tmpFile = new OpenFile(sector);
        directory -> FetchFrom(tmpFile);
        prev_token = token;
        token = strtok(NULL, "/");
    }
    DEBUG(dbgFile, "out of while");
    if(token == NULL)name = prev_token;
    else name = token;
    DEBUG(dbgFile, "file name is " << name);
    if (directory->Find(name) != -1)
      success = FALSE;			// file is already in directory
    else {	
        freeMap = new PersistentBitmap(freeMapFile,NumSectors);
        sector = freeMap->FindAndSet();	// find a sector to hold the file header
    	if (sector == -1) 		
            success = FALSE;		// no free block for file header 
        else if (!directory->Add(name, sector, TRUE))
            success = FALSE;	// no space in directory
	    else {
    	    hdr = new FileHeader;
            if (!hdr->Allocate(freeMap, initialSize))
                    success = FALSE;	// no space on disk for data
            else {	
                success = TRUE;
                // everthing worked, flush all changes back to disk
                hdr->WriteBack(sector); 		
                directory->WriteBack(tmpFile);
                freeMap->WriteBack(freeMapFile);
            }
            delete hdr;
        }
        delete freeMap;
    }
    delete directory;
    DEBUG(dbgFile, "create file end.");
    if(!firstTmp)delete tmpFile;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

//MP4 modified
OpenFile *FileSystem::Open(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    int sector;
    DEBUG(dbgFile, "Opening file" << name);
    directory -> FetchFrom(directoryFile);
    OpenFile *tmpFile = directoryFile;
    char *token = strtok(name, "/");
    char *prev_token = token;
    bool firstTmp = true;

    while(token != NULL){
        sector = directory -> Find(token);
        if(sector == -1||directory->IsFile(token))break;
        DEBUG(dbgFile, "Open file in directory " << token);
        if(firstTmp)firstTmp = false;
        else delete tmpFile;
        tmpFile = new OpenFile(sector);
        directory -> FetchFrom(tmpFile);
        prev_token = token;
        token = strtok(NULL, "/");
    }
    if(token == NULL)name = prev_token;
    else name = token;
    DEBUG(dbgFile, "Open file name is " << name);
    directory->FetchFrom(tmpFile);
    sector = directory->Find(name);
    if(sector >= 0){
        currentOpenedFile = new OpenFile(sector);
    }
    delete directory;
    if(!firstTmp)delete tmpFile;
    return currentOpenedFile;
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *name)
{ 
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    int sector;
    
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);

    //MP4 modified
    OpenFile *tmpFile = directoryFile;
    char *token = strtok(name, "/");
    char *prev_token = token;
    bool firstTmp = true;

    while(token != NULL){
        sector = directory -> Find(token);
        if(sector == -1||directory->IsFile(token))break;
        DEBUG(dbgFile, "open file in directory " << token);
        if(firstTmp)firstTmp = false;
        else delete tmpFile;
        tmpFile = new OpenFile(sector);
        directory -> FetchFrom(tmpFile);
        prev_token = token;
        token = strtok(NULL, "/");
    }
    if(token == NULL)name = prev_token;
    else name = token;

    directory -> FetchFrom(tmpFile);

    sector = directory->Find(name);
    if (sector == -1) {
       delete directory;
       if(!firstTmp)delete tmpFile;
       return FALSE;			 // file not found 
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new PersistentBitmap(freeMapFile,NumSectors);

    fileHdr->Deallocate(freeMap);  		// remove data blocks
    freeMap->Clear(sector);			// remove header block
    directory->Remove(name);

    freeMap->WriteBack(freeMapFile);		// flush to disk
    directory->WriteBack(tmpFile);        // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    if(!firstTmp)delete tmpFile;
    return TRUE;
} 

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List(char *name)
{
    Directory *directory = new Directory(NumDirEntries);

    directory->FetchFrom(directoryFile);
    //MP4 modified
    int sector;
    OpenFile *tmpFile = directoryFile;
    char *token = strtok(name, "/");
    bool firstTmp = true;
    while(token != NULL){
        if((sector = directory -> Find(token)) == -1)break;
        if(firstTmp)firstTmp = false;
        else delete tmpFile;
        tmpFile = new OpenFile(sector);
        directory -> FetchFrom(tmpFile);
        token = strtok(NULL, "/");
    }

    directory->List();
    delete directory;
    //cout<<"end delete directory"<<endl;
    delete token;
    //cout<<"end delete token"<<endl;
    if(!firstTmp)delete tmpFile;
    //cout<<"end if"<<endl;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
} 

#endif // FILESYS_STUB

//MP4 modified
bool FileSystem::CreateDirectory(char *name)
{
    Directory *directory;
    Directory *subDirectory;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;
    OpenFile *subDirectoryFile;
    
    DEBUG(dbgFile, "Creating directory's absolute path is " << name);

    directory = new Directory(NumDirEntries);
    directory -> FetchFrom(directoryFile);
    OpenFile *tmpFile = directoryFile;
    char *token = strtok(name, "/");
    char *prev_token = token;
    bool firstTmp = true;

    while(token != NULL){
        sector = directory -> Find(token);
        if(sector == -1)break;
        DEBUG(dbgFile, "current directory is " << token);
        if(firstTmp)firstTmp = false;
        else delete tmpFile;
        tmpFile = new OpenFile(sector);
        directory -> FetchFrom(tmpFile);
        prev_token = token;
        token = strtok(NULL, "/");
    }
    if(token == NULL)name = prev_token;
    else name = token;
    DEBUG(dbgFile, "directory name is " << name);
    subDirectory = new Directory(NumDirEntries);
    if(directory->Find(name) != -1)success = FALSE;
    else
    {
        freeMap = new PersistentBitmap(freeMapFile, NumSectors);
        sector = freeMap -> FindAndSet();
        if(sector == -1)success = FALSE;
        else if(!directory->Add(name, sector, FALSE))
            success = FALSE;
        else{
            hdr = new FileHeader;
            if(!hdr -> Allocate(freeMap, DirectoryFileSize))success = FALSE;
            else{
                success = TRUE;
                hdr -> WriteBack(sector);
                subDirectoryFile = new OpenFile(sector);
                subDirectory -> WriteBack(subDirectoryFile);
                directory -> WriteBack(tmpFile);
                freeMap -> WriteBack(freeMapFile);
            }
            delete hdr;
        }
        delete freeMap;
    }
    delete subDirectoryFile;
    delete subDirectory;
    delete directory;
    if(!firstTmp)delete tmpFile;
    return success;
}
int FileSystem::Read(char *buffer, int size, OpenFileId id)
{
    return currentOpenedFile -> Read(buffer, size);
}
int FileSystem::Write(char *buffer, int size, OpenFileId id)
{
    return currentOpenedFile -> Write(buffer, size);
}
int FileSystem::Close(OpenFileId id)
{
    delete currentOpenedFile;
    currentOpenedFile = NULL;
    return 1;
}
void FileSystem::RecursivelyList(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    int sector;
    OpenFile *tmpFile = directoryFile;
    char *token = strtok(name, "/");
    bool firstTmp = true;
    while(token != NULL){
        sector = directory -> Find(token);
        if(sector == -1)break;
        if(firstTmp)firstTmp = false;
        else delete tmpFile;
        tmpFile = new OpenFile(sector);
        directory -> FetchFrom(tmpFile);
        token = strtok(NULL, "/");
    }
    directory -> RecursivelyList();
    delete directory;
    if(!firstTmp)delete tmpFile;
}