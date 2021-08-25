// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "filehdr.h"
#include "debug.h"
#include "synchdisk.h"
#include "main.h"

//TODO
#define fileLevel2 NumDirect * SectorSize
#define fileLevel3 NumDirect * NumDirect * SectorSize
#define fileLevel4 NumDirect * NumDirect * NumDirect * SectorSize

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader()
{
	numBytes = -1;
	numSectors = -1;
	memset(dataSectors, -1, sizeof(dataSectors));
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader()
{
	// nothing to do now
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{ 
    numBytes = fileSize;
    numSectors  = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors)
	return FALSE;		// not enough space

	if(fileSize > fileLevel4){
		for(int i=0; fileSize > 0 && i < NumDirect; i++){
			dataSectors[i] = freeMap->FindAndSet();
			FileHeader* nextHDR = new FileHeader;
			if(fileSize > fileLevel4){
				nextHDR->Allocate(freeMap, fileLevel4);
			}
			else{
				nextHDR->Allocate(freeMap, fileSize);
			}
			fileSize -= fileLevel4;
			nextHDR->WriteBack(dataSectors[i]);
			delete nextHDR;
		}
	}
	else if(fileSize > fileLevel3){
		for(int i=0; fileSize > 0 && i < NumDirect; i++){
			dataSectors[i] = freeMap->FindAndSet();
			FileHeader* nextHDR = new FileHeader;
			if(fileSize > fileLevel3){
				nextHDR->Allocate(freeMap, fileLevel3);
			}
			else{
				nextHDR->Allocate(freeMap, fileSize);
			}
			fileSize -= fileLevel3;
			nextHDR->WriteBack(dataSectors[i]);
			delete nextHDR;
		}
	}
	else if(fileSize > fileLevel2){
		for(int i=0; fileSize > 0 && i < NumDirect; i++){
			dataSectors[i] = freeMap->FindAndSet();
			FileHeader* nextHDR = new FileHeader;
			if(fileSize > fileLevel2){
				nextHDR->Allocate(freeMap, fileLevel2);
			}
			else{
				nextHDR->Allocate(freeMap, fileSize);
			}
			fileSize -= fileLevel2;
			nextHDR->WriteBack(dataSectors[i]);
			delete nextHDR;
		}
	}
	else{
		for (int i = 0; i < numSectors; i++) {
			dataSectors[i] = freeMap->FindAndSet();
			// since we checked that there was enough free space,
			// we expect this to succeed
			ASSERT(dataSectors[i] >= 0);
		}
	}
    
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(PersistentBitmap *freeMap)
{
	if(numBytes > fileLevel2){
		FileHeader* nextHDR;
		for(int i=0; i< divRoundUp(numSectors, NumDirect);i++){
			nextHDR = new FileHeader;
			nextHDR->FetchFrom(dataSectors[i]);
			nextHDR->Deallocate(freeMap);
			delete nextHDR;
		}
	}
	else{
		for (int i = 0; i < numSectors; i++) {
			ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
			freeMap->Clear((int) dataSectors[i]);
		}
	}
    
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    kernel->synchDisk->ReadSector(sector, (char *)this);
	
	/*
		MP4 Hint:
		After you add some in-core informations, you will need to rebuild the header's structure
	*/
	
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    kernel->synchDisk->WriteSector(sector, (char *)this); 
	
	/*
		MP4 Hint:
		After you add some in-core informations, you may not want to write all fields into disk.
		Use this instead:
		char buf[SectorSize];
		memcpy(buf + offset, &dataToBeWritten, sizeof(dataToBeWritten));
		...
	*/
	
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
	int sector = -1;
	FileHeader* nextHDR = new FileHeader;
	if(numBytes > fileLevel4){
		//DEBUG(dbgFile, "ByteToSector4");
		sector = divRoundDown(offset, fileLevel4);
		nextHDR -> FetchFrom(dataSectors[sector]);
		sector = nextHDR->ByteToSector(offset - sector * fileLevel4);
	}
	else if(numBytes > fileLevel3){
		//DEBUG(dbgFile, "ByteToSector3");
		sector = divRoundDown(offset, fileLevel3);
		nextHDR -> FetchFrom(dataSectors[sector]);
		sector = nextHDR->ByteToSector(offset - sector * fileLevel3);
	}
	else if(numBytes > fileLevel2){
		//DEBUG(dbgFile, "ByteToSector2");
		sector = divRoundDown(offset, fileLevel2);
		
		nextHDR -> FetchFrom(dataSectors[sector]);
		sector = nextHDR->ByteToSector(offset - sector * fileLevel2);
	}
	else{
		//DEBUG(dbgFile, "ByteToSector1");
		sector = dataSectors[offset / SectorSize];
	}
	//DEBUG(dbgFile, "leave ByteToSector");
	delete nextHDR;
    return sector;
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
	printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
	if(numBytes > fileLevel2){
		OpenFile* openfile;
		for(int i=0; i<numSectors / NumDirect && i < NumDirect; i++){
			openfile = new OpenFile(dataSectors[i]);
			FileHeader *nextHDR = openfile->get_hdr();
			nextHDR->Print();
		}
	}
	else{
		int i, j, k;
		char *data = new char[SectorSize];
		
		for (i = 0; i < numSectors; i++)
			printf("%d ", dataSectors[i]);
		printf("\nFile contents:\n");
		for (i = k = 0; i < numSectors; i++) {
			kernel->synchDisk->ReadSector(dataSectors[i], data);
			for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
				if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
					printf("%c", data[j]);
				else
					printf("\\%x", (unsigned char)data[j]);
			}
			printf("\n"); 
		}
		delete [] data;
	}

}
void
FileHeader::self_Print(){
	/*printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
	if(numBytes > fileLevel2){
		OpenFile* openfile;
		for(int i=0; i<numSectors / NumDirect && i < NumDirect; i++){
			openfile = new OpenFile(dataSectors[i]);
			FileHeader *nextHDR = openfile->get_hdr();
			nextHDR->self_Print();
		}
	}
	else{
		int i, j, k;
		for (i = 0; i < numSectors; i++)
			printf("%d ", dataSectors[i]);
		
	}*/
	printf("FileHeader contents.  File size: %d.\n", numBytes);
	if(numBytes > fileLevel4){
		printf("This file has 4 level structure, ");
		int level = 1+ 30 + 30*30 + 1;
		printf("it use at least %d sectors for file header.\n", level);
	}
	else if(numBytes > fileLevel3){
		printf("This file has 3 level structure, ");
		int level = 1+30 + 1;
		level = level + divRoundUp((numBytes - fileLevel3), 120);
		printf("it use at least %d sectors for file header.\n", level);
	}
	else if(numBytes > fileLevel2){
		printf("This file has 2 level structure, ");
		int level = 2;
		
		printf("it use at least %d sectors for file header.\n", level);
	}
	else{
		printf("This file has 1 level structure, use only one sector for file header");
	}
}
