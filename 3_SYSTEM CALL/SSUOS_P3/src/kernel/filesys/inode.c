#include <filesys/inode.h>
#include <device/ata.h>
#include <device/block.h>
#include <bitmap.h>
#include <device/console.h>
#include <string.h>

//Block 12~15 valid inode number = 256
//inode size = 64byte, block size = 4096byte, 64 inode/block

char tmpblock[SSU_BLOCK_SIZE];
extern struct process *cur_process;

/* inode 테이블 초기화 */
void init_inode_table(struct ssu_fs *fs)
{
	struct bitmap *imap = fs->fs_inodemap;
	int i;

	for(i=0; i<NUM_INODEBLOCK; i++)
		fs_readblock(fs, SSU_INODE_BLOCK + i, ((char*)inode_tbl) + (i * SSU_BLOCK_SIZE));	

	if(!bitmap_test(imap, INO_ROOTDIR))
	{
		memset(inode_tbl, 0, sizeof(struct inode) * NUM_INODE);
		//Unvalid, Reserved
		bitmap_set(imap, 0, true);
		bitmap_set(imap, 1, true);

		inode_tbl[0].sn_ino = 0;
		inode_tbl[0].sn_size = 0;
		inode_tbl[0].sn_type = SSU_TYPE_DIR;
		inode_tbl[0].sn_nlink = 0;
		inode_tbl[0].sn_refcount = 0;
		inode_tbl[0].sn_fs = fs;

		//Root directory set

		make_dir(inode_tbl, ".");
		sync_inode(fs, inode_tbl + INO_ROOTDIR);
		sync_bitmapblock(fs);
	}

	cur_process->rootdir = inode_tbl + INO_ROOTDIR;
	cur_process->cwd = cur_process->rootdir;
}

struct inode* inode_create(struct ssu_fs *fs, uint16_t type)
{
	struct inode *new_inode;
	struct bitmap *imap = fs->fs_inodemap;
	int idx;

	for(idx = 0; idx <NUM_INODE; idx++)
		if(!bitmap_test(imap, idx))
			break;

	if(idx >= NUM_INODE) // 인덱스가 정해진 INODE의 개수보다 커지면 종료
		return NULL;

	new_inode = inode_tbl + idx;
	new_inode->sn_ino = idx;
	new_inode->sn_size = 0;
	new_inode->sn_type = type;
	new_inode->sn_nlink = 0;
	new_inode->sn_refcount = 0;
	new_inode->sn_fs = fs;

	bitmap_set(imap, idx, true);

	sync_inode(fs, new_inode);
	sync_bitmapblock(fs);

	return new_inode;
}

/* DISK에 요청한 데이터 쓰기 */
int inode_write(struct inode * in, uint32_t offset, char * buf, int len)
{
	int result=0;
	struct ssu_fs * fs = in->sn_fs;
	uint32_t blkoff = offset / SSU_BLOCK_SIZE; // the offset number of block
	uint32_t res_off = offset % SSU_BLOCK_SIZE; // 아마도 sector...?

	if(offset > in->sn_size) // 파일의 현재 위치로 넘어온 값이 파일의 크기보다 크다면 에러처리
		return -1;

	memset(tmpblock, 0, SSU_BLOCK_SIZE); // 임시 블록을 초기화처리
	if(res_off != 0 || blkoff < in->sn_nlink) // 남은 섹터의 수가 0이 아니거나 blkoff가 사용중인 direct block의 수보다 작으면
	{
		/* 데이터가 이미 블록에 존재한다는 의미이기 때문에, 기존에 저장되어 있던 데이터를 백업할 필요가 있어서 밑의 함수를 미리 호출하는 것으로 보임 */
		fs_readblock(fs, in->sn_directblock[blkoff], tmpblock); // 임시 블록에 데이터 읽어들이기
	}
	else // 블록이 비어있으면
	{
		balloc(fs->fs_blkmap, &(in->sn_directblock[blkoff])); // 블록을 할당해주고
		in->sn_nlink++; // 사용 중인 블록의 수를 증가시켜 주며
		sync_bitmapblock(fs); // 블록 동기화
	}

	memcpy(tmpblock + res_off, buf, len); // 해당 블록&섹터에 buf를 len만큼 쓰기

	fs_writeblock(fs, in->sn_directblock[blkoff], tmpblock);
	if(in->sn_size < offset+len)
		in->sn_size = offset+len; // 파일에 쓴 결과가 기존 파일 크기보다 크다면 파일 크기 확장
	sync_inode(fs, in); // inode값 동기화

	return result;
}

/* DISK에서 요청한 데이터 읽어오기 */
int inode_read(struct inode * in, uint32_t offset, char * buf, int len)
{
	int result=0;
	struct ssu_fs * fs = in->sn_fs;
	uint32_t blkoff = offset / SSU_BLOCK_SIZE;
	uint32_t res_off = offset % SSU_BLOCK_SIZE;

	if(offset > in->sn_size)
		return -1;
	
	memset(tmpblock, 0, SSU_BLOCK_SIZE);
	fs_readblock(fs, in->sn_directblock[blkoff], tmpblock);

	memcpy(buf, tmpblock + res_off, len);
	return result;
}

struct inode *inode_open(const char *pathname, int flags)
{
	struct inode *inode_cur;
	struct inode *new_in;
	struct direntry buf_dir;
	char fname_buf[FILENAME_LEN];
	int ndir;
	int i;

	inode_cur = cur_process->cwd; // 현재 작업중인 디렉토리를 inode_cur에 할당
	for(i=0; pathname[i] != '\0'; i++)
		fname_buf[i] = pathname[i]; // 파일이름 복사
	fname_buf[i] = '\0';

	ndir = num_direntry(inode_cur); // ndir : 디렉토리의 파일의 개수 
	for(i=0; i<ndir; i++)
	{   
		inode_read(inode_cur, i*sizeof(struct direntry), (char *)&buf_dir, sizeof(struct direntry));
		if(strncmp(buf_dir.de_name, fname_buf,FILENAME_LEN) == 0) return &(inode_tbl[buf_dir.de_ino]); // 중복 이름 존재, 생성 없이 리턴
	}

	new_in = inode_create(cur_process->cwd->sn_fs,SSU_TYPE_FILE);
	buf_dir.de_ino = new_in->sn_ino;
	strlcpy(buf_dir.de_name, fname_buf, FILENAME_LEN);
	inode_write(inode_cur, num_direntry(inode_cur)*sizeof(buf_dir), (char *)&buf_dir, sizeof(buf_dir)); //상위 inode에 추가

	return new_in;
}


static int sync_inode(struct ssu_fs *fs, struct inode* inode)
{
	int result = 0;
	int offset = inode->sn_ino / INODE_PER_BLOCK; // offset to current inode index from SSU_INODE_BLOCK 

	result = fs_writeblock(fs, SSU_INODE_BLOCK + offset, (char*)(inode_tbl + offset*INODE_PER_BLOCK));
	return result;
}

/* 이 아래로는 디렉토리 관련 함수 */

int make_dir(struct inode *cwd, char *name)
{
	struct direntry newde, cde, pde, tmp;
	struct inode * newin;
	struct ssu_fs *fs = cwd->sn_fs;
	int ndir, i;
	int len = strnlen(name, FILENAME_LEN);

	if(len > FILENAME_LEN || len <= 0)
	{
		printk("Unvalid filename length.\n");
		return -1;	
	}

	if(cwd->sn_ino >= INO_ROOTDIR)
	{
		ndir = num_direntry(cwd);
		for(i=0; i<ndir; i++)
		{
			inode_read(cwd, i*sizeof(struct direntry), (char*)&tmp, sizeof(struct direntry));
			if( strncmp(name, tmp.de_name, len) == 0 && tmp.de_name[len] == 0)
			{
				printk("Already exist filename.\n");
				return -1;
			}
		}
	}

	newin = inode_create(fs, SSU_TYPE_DIR);

	newde.de_ino = newin->sn_ino;
	strlcpy(newde.de_name, name, len+1);


	if(cwd->sn_ino >= INO_ROOTDIR)
		inode_write(cwd, cwd->sn_size, (char*)&newde, sizeof(struct direntry));
	else
		cwd = newin;

	cde.de_ino = newde.de_ino;
	strlcpy(cde.de_name, ".", 2);

	pde.de_ino = cwd->sn_ino;
	strlcpy(pde.de_name, "..", 3);	

	inode_write(newin, newin->sn_size, (char*)&pde, sizeof(struct direntry));
	inode_write(newin, newin->sn_size, (char*)&cde, sizeof(struct direntry));

	return 0;
}

static int num_direntry(struct inode *in)
{
	if(in->sn_size % sizeof(struct direntry) != 0 || in->sn_type != SSU_TYPE_DIR)
		return -1;

	return in->sn_size / sizeof(struct direntry);
}


void list_segment(struct inode *cwd)
{
	int i;
	int ndir = num_direntry(cwd);
	struct inode *in;
	struct direntry de;

	printk("name | size | type | blocks | ino\n");
	for(i=0; i<ndir; i++)
	{
		inode_read(cwd, i*sizeof(struct direntry), (char*)&de, sizeof(struct direntry));
		in = &inode_tbl[de.de_ino];

		printk("%s | %d | %c | %d | %d\n", de.de_name, in->sn_size, 
				(in->sn_type == SSU_TYPE_DIR) ? 'd' : 'n', in->sn_nlink, in->sn_ino);
	}
}

int change_dir(struct inode *cwd, char *path)
{
	int i;
	int ndir = num_direntry(cwd);
	struct inode *in;
	struct direntry de;
	int len = strnlen(path, FILENAME_LEN);

	if(path == 0)
	{
		cur_process->cwd = cur_process->rootdir;
		return -1;
	}

	for(i=0; i<ndir; i++)
	{
		inode_read(cwd, i*sizeof(struct direntry), (char*)&de, sizeof(struct direntry));

		if( strncmp(path, de.de_name, len) == 0 && de.de_name[len] == 0)
		{
			in = &inode_tbl[de.de_ino];
			if(in->sn_type != SSU_TYPE_DIR)
			{
				printk("Not a Directory.\n");
				return -1;
			}
			cur_process->cwd = in;
			return 0;
		}
	}
	printk("Not found directory\n");
	return -1;
}

int get_curde(struct inode *cwd, struct direntry * de)
{
	struct inode *pwd;
	int i, ndir;

	//get parent dir
	inode_read(cwd, 0, (char*)de, sizeof(struct direntry));
	pwd = &inode_tbl[de->de_ino];
	ndir = num_direntry(pwd);
	for(i=2; i<ndir; i++)
	{
		inode_read(pwd, i*sizeof(struct direntry), (char*)de, sizeof(struct direntry));
		if(de->de_ino == cwd->sn_ino)
			return 0;
	}
	return -1;
}
