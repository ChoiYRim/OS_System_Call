#include <proc/sched.h> // for scheduling function
#include <proc/proc.h> 
#include <device/device.h> // for dev_shutdown function
#include <interrupt.h>
#include <device/kbd.h>
#include <filesys/file.h>

pid_t do_fork(proc_func func, void* aux1, void* aux2)
{
	pid_t pid;
	struct proc_option opt;

	opt.priority = cur_process-> priority;
	pid = proc_create(func, &opt, aux1, aux2);

	return pid;
}

void do_exit(int status)
{
	cur_process->exit_status = status; 	//종료 상태 저장
	proc_free();						//프로세스 자원 해제
	do_sched_on_return();				//인터럽트 종료시 스케줄링
}

pid_t do_wait(int *status)
{
	while(cur_process->child_pid != -1)
		schedule();
	//SSUMUST : schedule 제거.

	int pid = cur_process->child_pid;
	cur_process->child_pid = -1;

	extern struct process procs[];
	procs[pid].state = PROC_UNUSED;

	if(!status)
		*status = procs[pid].exit_status;

	return pid;
}

void do_shutdown(void)
{
	dev_shutdown();
	return;
}

int do_ssuread(void)
{
	return kbd_read_char();
}

int do_open(const char *pathname, int flags) // append your code here!
{
	struct inode *inode;
	struct ssufile **file_cursor = cur_process->file;
	int fd;

	for(fd = 0; fd < NR_FILEDES; fd++)
		if(file_cursor[fd] == NULL) break;

	if (fd == NR_FILEDES) // 인덱스 초과
		return -1;

	if ((inode = inode_open(pathname, flags)) == NULL)
		return -1;
	
	if (inode->sn_type == SSU_TYPE_DIR) // 디렉토리 파일이라면 에러
		return -1;

	fd = file_open(inode,flags,0); // file open function with a flag -- file.c
	
	return fd;
}

int do_read(int fd, char *buf, int len)
{
	return generic_read(fd, (void *)buf, len); // ssulib.c
}
int do_write(int fd, const char *buf, int len)
{
	return generic_write(fd, (void *)buf, len); // ssulib.c
}

int do_fcntl(int fd, int cmd, long arg) // append your code here!
{
	int flag = 0; 
	struct ssufile **file_cursor = cur_process->file;
	struct ssufile * tmp_cursor;
	int ret,i;

	if(file_cursor[fd]->inode == NULL) // 파일이 열려있지 않다면
		return -1;

	if(cmd & F_DUPFD) // F_DUPFD 플래그
	{
		if(arg > NR_FILEDES)
			return -1;

/*
		for(i = arg; i < NR_FILEDES; i++)
		{
			if(file_cursor[i] == NULL)
			{
				file_cursor[i] = file_cursor[fd];
				break;
			}
		}

		return i;
*/
		if(file_cursor[fd]->flags & O_TRUNC)
		{
			file_cursor[fd]->flags &= ~O_TRUNC;
			flag |= O_TRUNC;
		}
		if(file_cursor[fd]->flags & O_APPEND)
		{
			file_cursor[fd]->flags &= ~O_APPEND;
			flag |= O_APPEND;
		}

		ret = file_open(file_cursor[fd]->inode,file_cursor[fd]->flags,0);

		if(ret < arg)
		{
			if(file_cursor[arg] == NULL)
			{
				file_cursor[arg] = file_cursor[ret];
				file_cursor[ret] = NULL;
				file_cursor[arg]->flags |= O_TRUNC;
				file_cursor[arg]->flags |= O_APPEND;
				return arg;
			}
			else
			{
				for(i = arg+1; i < NR_FILEDES; i++)
				{
					if(file_cursor[i] == NULL)
					{
						file_cursor[i] = file_cursor[ret];
						file_cursor[ret] = NULL;
						file_cursor[i]->flags |= O_TRUNC;
						file_cursor[i]->flags |= O_APPEND;
						return i;
					}
				}
			}
		}
	}
	else if(cmd & F_GETFL) // F_GETFL cmd - arg는 무시
	{
		return (int)file_cursor[fd]->flags;
	}
	else if(cmd & F_SETFL) // F_SETFL cmd - arg값을 가지고 플래그 설정
	{
		if(arg & O_RDONLY)
			flag |= O_RDONLY;
		if(arg & O_WRONLY)
			flag |= O_WRONLY;
		if(arg & O_RDWR)
			flag |= O_RDWR;
		if(arg & O_APPEND)
		{
			file_cursor[fd]->pos = file_cursor[fd]->inode->sn_size;
			flag |= O_APPEND;
		}
		if(arg & O_TRUNC)
		{
			uint32_t offset = file_cursor[fd]->pos;
			uint32_t tmpsize = file_cursor[fd]->inode->sn_size;
			char tmpbuf[tmpsize];

			inode_write(file_cursor[fd]->inode,offset,NULL,tmpsize);
			file_cursor[fd]->inode->sn_size = 0;
			flag |= O_TRUNC;
		}
		file_cursor[fd]->flags |= flag;

		return flag;
	}
	else // no cmd
		return 0;
}
