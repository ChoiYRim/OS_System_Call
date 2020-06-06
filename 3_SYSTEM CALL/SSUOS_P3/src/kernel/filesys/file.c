#include <filesys/inode.h>
#include <proc/proc.h>
#include <device/console.h>
#include <mem/palloc.h>

int file_open(struct inode *inode,int flags, int mode)
{
	int fd;
	struct ssufile **file_cursor = cur_process->file; // cur_process의 file은 ssufile 구조체

	for(fd = 0; fd < NR_FILEDES; fd++)
	{
		if(file_cursor[fd] == NULL) // 파일 디스크립터가 비어있음
		{
			if((file_cursor[fd] = (struct ssufile *)palloc_get_page()) == NULL)
				return -1; // 뭔지는 모르겠지만 파일 디스크립터가 비어있음에도 쓸 수 없음
			break;
		}	
	}
	
	inode->sn_refcount++;

	file_cursor[fd]->inode = inode; // inode값 할당
	file_cursor[fd]->pos = 0; // 오프셋을 SEEK_SET으로 설정

	/* you have to append your code here */

	if(flags & O_APPEND) // APPEND 요청
	{
		file_cursor[fd]->pos = file_cursor[fd]->inode->sn_size; // offset을 파일의 끝으로
	}
	else if(flags & O_TRUNC) // 기존의 파일 내용 비우고 다시 쓰기 - 블록을 초기화
	{
		uint32_t offset = cur_process->file[fd]->pos;
		uint32_t tmpsize = file_cursor[fd]->inode->sn_size;
		char tmpbuf[tmpsize];

		inode_write(file_cursor[fd]->inode,offset,NULL,tmpsize);
		file_cursor[fd]->inode->sn_size = 0;
	}

	file_cursor[fd]->flags = flags;
	file_cursor[fd]->unused = 0;

	return fd;
}

/* offset은 현재 파일의 offset,즉 pos를 의미하는 것 같음 */
int file_write(struct inode *inode, size_t offset, void *buf, size_t len)
{
	return inode_write(inode, offset, buf, len);
}

int file_read(struct inode *inode, size_t offset, void *buf, size_t len)
{
	return inode_read(inode, offset, buf, len);
}
