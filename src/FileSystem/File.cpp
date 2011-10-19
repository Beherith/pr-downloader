#include "File.h"
#include "FileSystem.h"
#include "Logger.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>


CFile::CFile(const std::string& filename, long size, int piecesize)
{
	handle=NULL;
//	LOG("CFile() size: %d\n",size);
	this->size=size;
	this->piecesize=piecesize;
	this->curpos=0;
	this->filename=filename;
	Open(filename);
}

CFile::~CFile()
{
	//TODO: write buffered data
	Close();
}
void CFile::Close()
{
	if (handle!=0)
		fclose(handle);
	handle=0;
}

bool CFile::Open(const std::string& filename)
{
	SetPieceSize(piecesize);
//	fileSystem->createSubdirs(filename);
	if (handle!=NULL) {
		LOG_ERROR("file opened before old was closed\n");
		return false;
	}
	struct stat sb;
	int res=stat(filename.c_str(), &sb);
	isnewfile=res!=0;
	if (isnewfile) { //check if file length is correct, if not set it
		handle=fopen(filename.c_str(), "w+");
	} else {
		handle=fopen(filename.c_str(), "r+");
	}
	if (handle<=0) {
		LOG_ERROR("open(%s): %s\n",filename.c_str(), strerror(errno));
		return false;
	}

	if((!isnewfile) && (size>0) && (size!=sb.st_size)) { //truncate file if real-size != excepted file size
		int ret=ftruncate(fileno(handle), size);
		if (ret!=0) {
			LOG_ERROR("ftruncate failed\n");
		}
		LOG_ERROR("File already exists but file-size missmatched\n");
	} else if (size<=0) {
		//TODO: allocate disk space
	}
	LOG_INFO("opened %s\n", filename.c_str());
	return true;
}

bool CFile::Hash(IHash& hash, int piece)
{
//	LOG("Hash() piece %d\n", piece);
	char buf[IO_BUF_SIZE];
	SetPos(0, piece);
	hash.Init();
	//	LOG("piece %d left: %d\n",piece,  GetPieceSize(piece));
	int read=0;
	long unsigned left=GetPieceSize(piece); //total bytes to hash
	while(left>0) {
		int toread=std::min(left, (long unsigned)sizeof(buf));
		read=Read(buf, toread, piece);
		if(read<=0) {
			LOG_ERROR("EOF or read error on piece %d, left: %d toread: %d size: %d, GetPiecePos %d GetPieceSize(): %d read: %d\n", piece, left, toread, GetPieceSize(piece), GetPiecePos(piece), GetPieceSize(piece), read);
			LOG_ERROR("curpos: %d\n", curpos);
			return false;
		}
		hash.Update(buf, toread);
		left=left-toread;
	}
	hash.Final();
	SetPos(0, piece);
//	LOG("CFile::Hash(): %s piece:%d\n", hash.toString().c_str(), piece);
	return true;
}

int CFile::Read(char*buf, int bufsize, int piece)
{
	SetPos(GetPiecePos(piece), piece);
//	LOG("Read() bufsize: %d GetPiecePos(%d): %d GetPiecePos: %d GetPieceSize() %d\n",bufsize, piece, GetPiecePos(piece), GetPiecePos(), GetPieceSize(piece));
	int items=fread(buf, bufsize, 1, handle);
	if (items<=0) {
		if(feof(handle)) {
			LOG_ERROR("EOF while Read: '%s'!\n", strerror(errno));
			return -1;
		}
		if(ferror(handle)) {
			LOG_ERROR("read error %s bufsize: %d curpos: %d GetPieceSize: %d\n", strerror(errno), bufsize, curpos, GetPieceSize());
			SetPos(0, piece);
			return items;
		}
	}
	SetPos(GetPiecePos(piece)+bufsize, piece); //inc pos
	return bufsize;
}

void CFile::SetPos(long pos, int piece)
{
//	LOG("SetPos() pos %d piece%d\n", pos, piece);
	if (piece>=0) {
		assert(pieces[piece].pos<=size+pos);
		assert(pos<=piecesize);
		pieces[piece].pos =pos;
	} else {
		assert(size<=0 || pos<=size);
		curpos = pos;
	}
	Seek(pos, piece);
}

int CFile::Write(const char*buf, int bufsize, int piece)
{
//	LOG("Write() bufsize %d piece %d handle %d\n", bufsize, piece, fileno(handle));
	SetPos(GetPiecePos(piece), piece);
	int res=fwrite(buf, bufsize, 1, handle);
	if (res!=1)
		LOG_ERROR("write error %d\n", res);
//	LOG("wrote bufsize %d\n", bufsize);
	if(ferror(handle)!=0) {
		LOG_ERROR("Error in write(): %s\n", strerror(errno));
		abort();
	}
	if(feof(handle)) {
		LOG_ERROR("EOF in write(): %s\n", strerror(errno));
	}
	SetPos(GetPiecePos(piece)+bufsize, piece);
	/*	if ((piece>=0) && (GetPiecePos(piece)==GetPieceSize(piece))) {
			LOG("piece finished: %d\n", piece);
		}
	*/
	return bufsize;
}


int CFile::Seek(unsigned long pos, int piece)
{
//	LOG("Seek() pos: %d piece: %d\n", pos, piece);
	assert(piece<=(int)pieces.size());
	if(piece>=0) { //adjust position relative to piece pos
		pos=this->piecesize*piece+pos;
	}
	if (fseek(handle, pos, SEEK_SET)!=0) {
		LOG_ERROR("seek error %ld\n", pos);
	}
	return pos;
}

bool CFile::SetPieceSize(int pieceSize)
{
	assert(handle==NULL); //this function has to be called before the file is opened
	pieces.clear();
	if ((size<=0) || (pieceSize<=0)) {
		LOG_DEBUG("SetPieceSize(): FileSize:%ld PieceSize: %d\n", size, pieceSize);
		return false;
	}
	int count=this->size/pieceSize;
	if(this->size%pieceSize>0)
		count++;
	if(count<=0) {
		LOG_ERROR("SetPieceSize(): count<0\n");
		return false;
	}
	for(int i=0; i<count; i++) {
		pieces.push_back(CFilePiece());
	}
	piecesize=pieceSize;
	curpos=0;
//	LOG("SetPieceSize piecesize: %d filesize: %ld pieces count:%d\n", pieceSize, this->size, (int)pieces.size());
	return true;
}

int CFile::GetPieceSize(int piece)
{
	if (piece>=0) {
		assert(piece<=(int)pieces.size());
		if ((int)pieces.size()-1==piece) //last piece
			return size%piecesize;
//		LOG("GetPieceSize piece %d, pieces.size() %d piecesize: %d size %d \n", piece, pieces.size(),piecesize, size);
		return piecesize;
	}
	return size;
}

long CFile::GetPiecePos(int piece)
{
	assert(piece<=(int)pieces.size());
	if (piece>=0)
		return pieces[piece].pos;
	return curpos;
}

long CFile::GetSize()
{
	if(handle==NULL) {
		LOG_ERROR("GetSize(): file isn't opened!\n");
		return -1;
	}

	struct stat sb;
	if (fstat(fileno(handle), &sb)!=0) {
		LOG_ERROR("CFile::SetSize(): fstat failed\n");
		return -1;
	}
	return sb.st_size;
}

bool CFile::IsNewFile()
{
	return isnewfile;
}
